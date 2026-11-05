#include "snapshot.hpp"

#include "child_process.hpp"
#include "server/storage/database.hpp"
#include "server/storage/database_fwd.hpp"
#include "server/storage/object.hpp"

#include <cereal/archives/portable_binary.hpp>
#include <cereal/cereal.hpp>
#include <cereal/details/helpers.hpp>
#include <cereal/types/string.hpp> // IWYU pragma: keep (std::string serialization)
#include <fmt/format.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

// File layout (cereal portable binary, little-endian):
//
//   magic     8 bytes   "RCPPSNAP"
//   version   uint32    FORMAT_VERSION
//   count     uint64    number of entries
//   entries   count x { key: string, deadline_ms: int64 (-1 = none),
//                       type: uint8, payload }
//
//   payload by type:  string  string
//                     list    uint64 n, n x string (head to tail)
//                     set     uint64 n, n x string
//                     hash    uint64 n, n x (field: string, value: string)
//
// Bump FORMAT_VERSION on any change; loading rejects other versions rather
// than misreading them.
//
// cereal reports a failed write or a short read by throwing. That is the only
// exception here, and it ends at write_snapshot_file() and load_snapshot(),
// which turn it into an error.
namespace redis::persistence {
namespace {
namespace fs = std::filesystem;

constexpr std::array<char, 8> MAGIC{'R', 'C', 'P', 'P', 'S', 'N', 'A', 'P'};
constexpr uint32_t FORMAT_VERSION = 1;
constexpr int64_t NO_DEADLINE     = -1;

using OutArchive = cereal::PortableBinaryOutputArchive;
using InArchive  = cereal::PortableBinaryInputArchive;

int64_t to_unix_ms(Database::time_point tp) {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
			   tp.time_since_epoch())
		.count();
}

Database::time_point from_unix_ms(int64_t ms) {
	return Database::time_point(std::chrono::milliseconds(ms));
}

template <typename Range>
void write_strings(OutArchive &out, const Range &range) {
	out(static_cast<uint64_t>(std::size(range)));
	for (const String &s : range) out(s);
}

/// Write \p s exactly as cereal writes a std::string: a size tag, then the
/// bytes. Keys and embedded values have no std::string to hand cereal.
void write_string(OutArchive &out, std::string_view s) {
	out(cereal::make_size_tag(static_cast<cereal::size_type>(s.size())));
	out(cereal::binary_data(s.data(), s.size()));
}

void write_object(OutArchive &out, const Object &object) {
	out(static_cast<uint8_t>(object.type()));
	object.visit([&out]<typename T>(const T &value) {
		if constexpr (std::is_same_v<T, std::string_view>) {
			write_string(out, value);
		} else if constexpr (std::is_same_v<T, Hash>) {
			out(static_cast<uint64_t>(value.size()));
			for (const auto &[field, v] : value) out(field, v);
		} else {
			write_strings(out, value);
		}
	});
}

uint64_t read_size(InArchive &in) {
	uint64_t n = 0;
	in(n);
	return n;
}

/// The value at the archive's position, or why it is not one. (A short read
/// still throws, from cereal.)
std::expected<Object, std::string> read_object(InArchive &in) {
	uint8_t type = 0;
	in(type);
	switch (type) {
	case 0: {
		String s;
		in(s);
		return Object(std::move(s));
	}
	case 1: {
		List list;
		for (auto n = read_size(in); n > 0; --n) in(list.emplace_back());
		return make_object(std::move(list));
	}
	case 2: {
		Set set;
		for (auto n = read_size(in); n > 0; --n) {
			String member;
			in(member);
			set.insert(std::move(member));
		}
		return make_object(std::move(set));
	}
	case 3: {
		Hash hash;
		for (auto n = read_size(in); n > 0; --n) {
			String field;
			String value;
			in(field, value);
			hash.insert_or_assign(std::move(field), std::move(value));
		}
		return make_object(std::move(hash));
	}
	default: return std::unexpected(fmt::format("unknown value type {}", type));
	}
}

struct Entry {
	std::string key;
	int64_t deadline_ms;
	const Object *value;
};

/// Write every live key of \p db to \p tmp. The file is closed when this
/// returns, so the caller may sync, rename or remove it.
std::expected<void, std::string> write_snapshot_file(const Database &db,
													 const fs::path &tmp) {
	// Collect in one pass so the header count and the entry count always agree,
	// even if keys expire between a count pass and a write pass.
	std::vector<Entry> entries;
	db.for_each([&](std::string_view key,
					const Object &value,
					std::optional<Database::time_point> deadline) {
		entries.push_back({std::string(key),
						   deadline ? to_unix_ms(*deadline) : NO_DEADLINE,
						   &value});
	});

	std::ofstream stream(tmp, std::ios::binary | std::ios::trunc);
	if (!stream)
		return std::unexpected(fmt::format("cannot open {}", tmp.string()));

	try {
		OutArchive out(stream);
		for (const char c : MAGIC) out(c);
		out(FORMAT_VERSION);
		out(static_cast<uint64_t>(entries.size()));
		for (const Entry &e : entries) {
			write_string(out, e.key);
			out(e.deadline_ms);
			write_object(out, *e.value);
		}
	} catch (const std::exception &e) {
		return std::unexpected(e.what());
	}

	stream.flush();
	if (!stream) return std::unexpected("write failed");
	return {};
}
} // namespace

fs::path snapshot_temp_path(const fs::path &file, posix::pid_type pid) {
	return fs::path(file).concat(fmt::format(".tmp-{}", pid));
}

std::expected<void, std::string> save_snapshot(const Database &db,
											   const fs::path &file) {
	const fs::path tmp = snapshot_temp_path(file, posix::current_pid());

	// Reports the step that failed and leaves no temporary file behind.
	const auto failed = [&](std::string_view why) {
		std::error_code ignored;
		fs::remove(tmp, ignored);
		return std::unexpected(
			fmt::format("saving {} failed: {}", file.string(), why));
	};

	if (auto written = write_snapshot_file(db, tmp); !written)
		return failed(written.error());
	if (const auto ec = posix::sync_file(tmp))
		return failed(fmt::format("fsync: {}", ec.message()));

	std::error_code ec;
	fs::rename(tmp, file, ec);
	if (ec) return failed(fmt::format("rename: {}", ec.message()));
	if (ec = posix::sync_directory(file.parent_path()); ec)
		return failed(fmt::format("fsync of the directory: {}", ec.message()));
	return {};
}

std::expected<void, std::string> load_snapshot(Database &db,
											   const fs::path &file,
											   bool keep_expired) {
	std::ifstream stream(file, std::ios::binary);
	if (!stream) {
		if (!fs::exists(file)) return {};
		return std::unexpected(fmt::format("cannot open {}", file.string()));
	}

	try {
		InArchive in(stream);
		std::array<char, 8> magic{};
		for (char &c : magic) in(c);
		uint32_t version = 0;
		in(version);
		if (magic != MAGIC)
			return std::unexpected(
				fmt::format("{} is not a snapshot file", file.string()));
		if (version != FORMAT_VERSION)
			return std::unexpected(
				fmt::format("{} has format version {}, expected {}",
							file.string(),
							version,
							FORMAT_VERSION));

		// Load into a scratch keyspace so a corrupt file leaves db untouched.
		Database loaded;
		// Stores a deadline in the past instead of deleting its key.
		loaded.set_expiry_enabled(!keep_expired);
		const auto now = Database::clock::now();
		for (auto n = read_size(in); n > 0; --n) {
			String key;
			int64_t deadline_ms = 0;
			in(key, deadline_ms);
			auto value = read_object(in);
			if (!value)
				return std::unexpected(fmt::format(
					"{} is corrupt: {}", file.string(), value.error()));

			if (deadline_ms == NO_DEADLINE) {
				loaded.set(key, std::move(*value));
			} else if (const auto deadline = from_unix_ms(deadline_ms);
					   keep_expired || deadline > now) {
				loaded.set(key, std::move(*value));
				loaded.set_expiry(key, deadline);
			}
		}
		loaded.set_expiry_enabled(true);
		db = std::move(loaded);
	} catch (const std::exception &e) {
		return std::unexpected(fmt::format("{} is corrupt or truncated: {}",
										   file.string(),
										   e.what()));
	}
	return {};
}

} // namespace redis::persistence
