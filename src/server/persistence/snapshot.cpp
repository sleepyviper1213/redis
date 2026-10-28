#include "snapshot.hpp"

#include "posix.hpp"
#include "server/storage/database.hpp"

#include <cereal/archives/portable_binary.hpp>
#include <cereal/types/string.hpp>
#include <fmt/format.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

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
	for (const std::string &s : range) out(s);
}

void write_object(OutArchive &out, const Object &object) {
	out(static_cast<uint8_t>(object.index()));
	std::visit(
		[&out]<typename T>(const T &value) {
			if constexpr (std::is_same_v<T, String>) out(value);
			else if constexpr (std::is_same_v<T, Hash>) {
				out(static_cast<uint64_t>(value.size()));
				for (const auto &[field, v] : value) out(field, v);
			} else write_strings(out, value);
		},
		object);
}

uint64_t read_size(InArchive &in) {
	uint64_t n = 0;
	in(n);
	return n;
}

Object read_object(InArchive &in) {
	uint8_t type = 0;
	in(type);
	switch (type) {
	case 0: {
		String s;
		in(s);
		return s;
	}
	case 1: {
		List list;
		for (auto n = read_size(in); n > 0; --n) in(list.emplace_back());
		return list;
	}
	case 2: {
		Set set;
		for (auto n = read_size(in); n > 0; --n) {
			std::string member;
			in(member);
			set.insert(std::move(member));
		}
		return set;
	}
	case 3: {
		Hash hash;
		for (auto n = read_size(in); n > 0; --n) {
			std::string field, value;
			in(field, value);
			hash.insert_or_assign(std::move(field), std::move(value));
		}
		return hash;
	}
	default:
		throw cereal::Exception(fmt::format("unknown value type {}", type));
	}
}
} // namespace

std::expected<void, std::string> save_snapshot(const Database &db,
											   const fs::path &file) {
	// Unique per process, so a SAVE and a BGSAVE child never share a file.
	const fs::path tmp =
		fs::path(file).concat(fmt::format(".tmp-{}", posix::current_pid()));
	try {
		{
			std::ofstream stream(tmp, std::ios::binary | std::ios::trunc);
			if (!stream)
				return std::unexpected(
					fmt::format("cannot open {}", tmp.string()));
			stream.exceptions(std::ios::badbit | std::ios::failbit);

			OutArchive out(stream);
			for (char c : MAGIC) out(c);
			out(FORMAT_VERSION);

			uint64_t count = 0;
			db.for_each([&](auto &&...) { ++count; });
			out(count);

			db.for_each([&](const std::string &key,
							const Object &value,
							std::optional<Database::time_point>
								deadline) {
				out(key, deadline ? to_unix_ms(*deadline) : NO_DEADLINE);
				write_object(out, value);
			});
		}
		if (auto ec = posix::sync_file(tmp))
			throw fs::filesystem_error("fsync", tmp, ec);
		fs::rename(tmp, file);
		if (auto ec = posix::sync_directory(file.parent_path()))
			throw fs::filesystem_error("fsync", file.parent_path(), ec);
	} catch (const std::exception &e) {
		std::error_code ignored;
		fs::remove(tmp, ignored);
		return std::unexpected(
			fmt::format("saving {} failed: {}", file.string(), e.what()));
	}
	return {};
}

std::expected<void, std::string> load_snapshot(Database &db,
											   const fs::path &file) {
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
		const auto now = Database::clock::now();
		for (auto n = read_size(in); n > 0; --n) {
			std::string key;
			int64_t deadline_ms = 0;
			in(key, deadline_ms);
			Object value = read_object(in);

			if (deadline_ms == NO_DEADLINE) {
				loaded.set(key, std::move(value));
			} else if (const auto deadline = from_unix_ms(deadline_ms);
					   deadline > now) {
				loaded.set(key, std::move(value));
				loaded.set_expiry(key, deadline);
			}
		}
		db = std::move(loaded);
	} catch (const std::exception &e) {
		return std::unexpected(fmt::format("{} is corrupt or truncated: {}",
										   file.string(),
										   e.what()));
	}
	return {};
}

} // namespace redis::persistence
