#include "snapshot.hpp"

#include "common/store/database.hpp"
#include "common/store/database_fwd.hpp"
#include "common/store/object.hpp"
#include "core/chrono/deadline_timer.hpp"
#include "core/error/failed.hpp"
#include "core/os/child_process.hpp"
#include "core/os/save_file.hpp"

#include <fmt/std.h>
#include <rfl/Literal.hpp>
#include <rfl/TaggedUnion.hpp>
#include <rfl/Vectorstring.hpp>
#include <rfl/cbor.hpp>

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

// File layout (integers little-endian):
//
//   magic     8 bytes   "RCPPSNAP"
//   version   uint32    FORMAT_VERSION
//   count     uint64    number of entries
//   entries   count x { size: uint64, record: size bytes of CBOR }
//
// Each record is a Record (below) encoded by reflect-cpp: a CBOR map with the
// key, the deadline if there is one, and the value tagged by its type. Keys and
// values are CBOR byte strings, since they need not be UTF-8. Records are
// framed one by one so that saving holds one entry's copy at a time, not the
// keyspace's, and a bad size cannot make loading allocate past the file.
//
// Bump FORMAT_VERSION on any change; loading rejects other versions rather
// than misreading them. Version 1 was cereal's portable binary.
namespace redis::persistence::detail {
namespace {
namespace fs = std::filesystem;

constexpr std::array<char, 8> MAGIC{'R', 'C', 'P', 'P', 'S', 'N', 'A', 'P'};
constexpr uint32_t FORMAT_VERSION = 2;

/// Binary-safe bytes: reflect-cpp writes these as a CBOR byte string.
using Bytes = rfl::Vectorstring;

struct StringValue {
	using Tag = rfl::Literal<"string">;
	Bytes value;
};

struct ListValue {
	using Tag = rfl::Literal<"list">;
	std::vector<Bytes> items; ///< head to tail
};

struct SetValue {
	using Tag = rfl::Literal<"set">;
	std::vector<Bytes> members;
};

struct HashValue {
	using Tag = rfl::Literal<"hash">;
	std::vector<std::pair<Bytes, Bytes>> fields;
};

using Value =
	rfl::TaggedUnion<"type", StringValue, ListValue, SetValue, HashValue>;

/// One key of the keyspace, as the file holds it.
struct Record {
	Bytes key;
	std::optional<int64_t> deadline_ms; ///< absent without a TTL
	Value value;
};

Bytes to_bytes(std::string_view s) { return {s.begin(), s.end()}; }

String to_string(const Bytes &b) { return {b.data(), b.size()}; }

/// A keyspace value as a record's Value.
struct ToValue {
	Value operator()(std::string_view s) const {
		return StringValue{.value = to_bytes(s)};
	}

	Value operator()(const List &list) const {
		ListValue out;
		out.items.reserve(list.size());
		for (const String &s : list) out.items.push_back(to_bytes(s));
		return out;
	}

	Value operator()(const Set &set) const {
		SetValue out;
		out.members.reserve(set.size());
		for (const String &s : set) out.members.push_back(to_bytes(s));
		return out;
	}

	Value operator()(const Hash &hash) const {
		HashValue out;
		out.fields.reserve(hash.size());
		for (const auto &[field, v] : hash)
			out.fields.emplace_back(to_bytes(field), to_bytes(v));
		return out;
	}
};

/// A record's Value as a keyspace value.
struct ToObject {
	Object operator()(const StringValue &v) const {
		return Object(to_string(v.value));
	}

	Object operator()(const ListValue &v) const {
		List list;
		for (const Bytes &b : v.items) list.emplace_back(to_string(b));
		return make_object(std::move(list));
	}

	Object operator()(const SetValue &v) const {
		Set set;
		for (const Bytes &b : v.members) set.insert(to_string(b));
		return make_object(std::move(set));
	}

	Object operator()(const HashValue &v) const {
		Hash hash;
		for (const auto &[field, value] : v.fields)
			hash.insert_or_assign(to_string(field), to_string(value));
		return make_object(std::move(hash));
	}
};

template <typename T>
void write_le(std::ostream &out, T value) {
	if constexpr (std::endian::native == std::endian::big)
		value = std::byteswap(value);
	std::array<char, sizeof(T)> bytes{};
	std::memcpy(bytes.data(), &value, sizeof(T));
	out.write(bytes.data(), bytes.size());
}

/// The next \p T in \p in, or nothing on a short read.
template <typename T>
std::optional<T> read_le(std::istream &in) {
	std::array<char, sizeof(T)> bytes{};
	if (!in.read(bytes.data(), bytes.size())) return std::nullopt;
	T value{};
	std::memcpy(&value, bytes.data(), sizeof(T));
	if constexpr (std::endian::native == std::endian::big)
		value = std::byteswap(value);
	return value;
}

struct Entry {
	std::string_view key;
	std::optional<int64_t> deadline_ms;
	const Object *value;
};

/// Write every live key of \p db to \p stream, the file \p file is being
/// replaced with.
std::expected<void, PersistError>
write_snapshot(const Database &db, const fs::path &file, std::ostream &stream) {
	// Collect in one pass so the header count and the entry count always agree,
	// even if keys expire between a count pass and a write pass. The views stay
	// valid: nothing writes to db meanwhile.
	std::vector<Entry> entries;
	db.for_each([&](std::string_view key,
					const Object &value,
					std::optional<Database::time_point>
						deadline) {
		entries.push_back(
			{.key = key,
			 .deadline_ms =
				 deadline ? std::optional(deadline->to_msecs_since_epoch())
						  : std::nullopt,
			 .value = &value});
	});

	stream.write(MAGIC.data(), MAGIC.size());
	write_le(stream, FORMAT_VERSION);
	write_le(stream, static_cast<uint64_t>(entries.size()));
	for (const Entry &e : entries) {
		const Record record{.key         = to_bytes(e.key),
							.deadline_ms = e.deadline_ms,
							.value       = e.value->visit(ToValue{})};
		const std::vector<char> encoded = rfl::cbor::write(record);
		write_le(stream, static_cast<uint64_t>(encoded.size()));
		stream.write(encoded.data(),
					 static_cast<std::streamsize>(encoded.size()));
		if (!stream) break;
	}

	stream.flush();
	if (!stream)
		REDIS_FAILED(
			PersistError{.kind = PersistKind::SnapshotEncode, .path = file});
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
	if (!save) REDIS_FAILED(to_persist_error(save.error()));
	if (auto written = write_snapshot(db, file, save->stream()); !written)
		return written;
	if (auto done = save->commit(); !done)
		REDIS_FAILED(to_persist_error(done.error()));
	return {};
}

std::expected<void, PersistError>
load_snapshot(Database &db, const fs::path &file, bool keep_expired) {
	// What is wrong with this file, for the places below that find something.
	const auto bad = [&file](PersistKind kind,
							 std::uint64_t at   = 0,
							 std::int64_t code  = 0,
							 std::string quoted = {}) {
		REDIS_FAILED(PersistError{.kind   = kind,
								  .path   = file,
								  .at     = at,
								  .code   = code,
								  .quoted = std::move(quoted)});
	};
	const auto corrupt = [&bad](std::string why) {
		return bad(PersistKind::SnapshotCorrupt, 0, 0, std::move(why));
	};

	std::ifstream stream(file, std::ios::binary);
	if (!stream) {
		if (!fs::exists(file)) return {};
		return bad(PersistKind::CannotOpen);
	}
	std::error_code ec;
	const std::uintmax_t file_size = fs::file_size(file, ec);
	if (ec) return bad(PersistKind::CannotOpen);

	std::array<char, 8> magic{};
	if (!stream.read(magic.data(), magic.size()) || magic != MAGIC)
		return bad(PersistKind::NotASnapshot);
	const auto version = read_le<uint32_t>(stream);
	if (!version) return bad(PersistKind::NotASnapshot);
	if (*version != FORMAT_VERSION)
		return bad(PersistKind::SnapshotVersion, *version, FORMAT_VERSION);
	const auto count = read_le<uint64_t>(stream);
	if (!count) return corrupt("truncated header");

	// Load into a scratch keyspace so a corrupt file leaves db untouched.
	Database loaded;
	// Stores a deadline in the past instead of deleting its key.
	loaded.set_expiry_enabled(!keep_expired);
	const auto now = DateTime::current();
	std::vector<char> buffer;
	for (uint64_t n = *count; n > 0; --n) {
		const auto size = read_le<uint64_t>(stream);
		if (!size) return corrupt("truncated entry");
		const auto offset = static_cast<std::uintmax_t>(stream.tellg());
		if (*size > file_size - offset) return corrupt("truncated entry");
		buffer.resize(*size);
		if (!stream.read(buffer.data(), static_cast<std::streamsize>(*size)))
			return corrupt("truncated entry");

		auto record = rfl::cbor::read<Record>(buffer);
		if (!record) return corrupt(record.error().what());

		const String key = to_string(record->key);
		Object value     = record->value.visit(ToObject{});
		if (!record->deadline_ms) {
			loaded.set(key, std::move(value));
		} else if (const auto deadline =
					   DateTime::from_msecs_since_epoch(*record->deadline_ms);
				   keep_expired || deadline > now) {
			loaded.set(key, std::move(value));
			loaded.set_expiry(key, DeadlineTimer::at(deadline));
		}
	}
	loaded.set_expiry_enabled(true);
	db = std::move(loaded);
	return {};
}

} // namespace redis::persistence::detail
