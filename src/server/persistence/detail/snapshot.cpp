#include "snapshot.hpp"
#include "core/error/failed.hpp"

#include "core/os/child_process.hpp"
#include "core/chrono/deadline_timer.hpp"
#include "common/store/object.hpp"
#include "core/os/save_file.hpp"
#include "common/store/database.hpp"
#include "common/store/database_fwd.hpp"

#include <cereal/archives/portable_binary.hpp>
#include <cereal/cereal.hpp>
#include <cereal/details/helpers.hpp>
#include <cereal/types/string.hpp> // IWYU pragma: keep (std::string serialization)
#include <fmt/std.h>

#include <array>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <ostream>
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
namespace redis::persistence::detail {
namespace {
namespace fs = std::filesystem;

constexpr std::array<char, 8> MAGIC{'R', 'C', 'P', 'P', 'S', 'N', 'A', 'P'};
constexpr uint32_t FORMAT_VERSION = 1;
constexpr int64_t NO_DEADLINE     = -1;

using OutArchive = cereal::PortableBinaryOutputArchive;
using InArchive  = cereal::PortableBinaryInputArchive;

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

/// The number of a value type this version does not know.
struct UnknownValueType {
	uint8_t number;
};

/// The value at the archive's position, or why it is not one. (A short read
/// still throws, from cereal.)
std::expected<Object, UnknownValueType> read_object(InArchive &in) {
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
	default: FAILED(UnknownValueType{type});
	}
}

struct Entry {
	std::string key;
	int64_t deadline_ms;
	const Object *value;
};

/// Write every live key of \p db to \p stream, the file \p file is being
/// replaced with.
std::expected<void, PersistError> write_snapshot(const Database &db,
												 const fs::path &file,
												 std::ostream &stream) {
	// What a failed write looks like; quoted is empty for a plain stream failure.
	const auto failed = [&file](std::string reason) {
		FAILED(PersistError{.kind   = PersistKind::SnapshotEncode,
											.path   = file,
											.quoted = std::move(reason)});
	};

	// Collect in one pass so the header count and the entry count always agree,
	// even if keys expire between a count pass and a write pass.
	std::vector<Entry> entries;
	db.for_each([&](std::string_view key,
					const Object &value,
					std::optional<Database::time_point>
						deadline) {
		entries.emplace_back(
			std::string(key),
			deadline ? deadline->to_msecs_since_epoch()
					 : NO_DEADLINE,
			&value);
	});

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
	} catch (const std::exception &e) { return failed(e.what()); }

	stream.flush();
	if (!stream) return failed({});
	return {};
}
} // namespace

fs::path snapshot_temp_path(const fs::path &file, os::pid_type pid) {
	return fmt::format("{}.tmp-{}", file, pid);
}

std::expected<void, PersistError> save_snapshot(const Database &db,
												const fs::path &file) {
	// The SaveFile removes its temporary file on every way out but a successful
	// commit, so a failure at any step leaves nothing behind.
	auto save =
		os::SaveFile::open(file, snapshot_temp_path(file, os::current_pid()));
	if (!save) FAILED(to_persist_error(save.error()));
	if (auto written = write_snapshot(db, file, save->stream()); !written)
		return written;
	if (auto done = save->commit(); !done)
		FAILED(to_persist_error(done.error()));
	return {};
}

std::expected<void, PersistError>
load_snapshot(Database &db, const fs::path &file, bool keep_expired) {
	// What is wrong with this file, for the places below that find something.
	const auto bad = [&file](PersistKind kind, std::uint64_t at = 0,
							 std::int64_t code = 0, std::string quoted = {}) {
		FAILED(PersistError{.kind   = kind,
											.path   = file,
											.at     = at,
											.code   = code,
											.quoted = std::move(quoted)});
	};

	std::ifstream stream(file, std::ios::binary);
	if (!stream) {
		if (!fs::exists(file)) return {};
		return bad(PersistKind::CannotOpen);
	}

	try {
		InArchive in(stream);
		std::array<char, 8> magic{};
		for (char &c : magic) in(c);
		uint32_t version = 0;
		in(version);
		if (magic != MAGIC) return bad(PersistKind::NotASnapshot);
		if (version != FORMAT_VERSION)
			return bad(PersistKind::SnapshotVersion, version, FORMAT_VERSION);

		// Load into a scratch keyspace so a corrupt file leaves db untouched.
		Database loaded;
		// Stores a deadline in the past instead of deleting its key.
		loaded.set_expiry_enabled(!keep_expired);
		const auto now = DateTime::current();
		for (auto n = read_size(in); n > 0; --n) {
			String key;
			int64_t deadline_ms = 0;
			in(key, deadline_ms);
			auto value = read_object(in);
			if (!value)
				return bad(PersistKind::SnapshotValueType, value.error().number);

			if (deadline_ms == NO_DEADLINE) {
				loaded.set(key, std::move(*value));
			} else if (const auto deadline = DateTime::from_msecs_since_epoch(deadline_ms);
					   keep_expired || deadline > now) {
				loaded.set(key, std::move(*value));
				loaded.set_expiry(key, DeadlineTimer::at(deadline));
			}
		}
		loaded.set_expiry_enabled(true);
		db = std::move(loaded);
	} catch (const std::exception &e) {
		return bad(PersistKind::SnapshotCorrupt, 0, 0, e.what());
	}
	return {};
}

} // namespace redis::persistence::detail
