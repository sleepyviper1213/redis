#include "append_only_file.hpp"

#include "../log_channels.hpp"
#include "common/resp/parser.hpp"
#include "common/resp/request.hpp"
#include "../commands/handler.hpp"
#include "common/store/database.hpp" // IWYU pragma: keep (db.drain_expired())
#include "core/os/save_file.hpp"
#include "detail/snapshot.hpp"

#include <fmt/format.h>
#include <fmt/std.h>
#include <spdlog/logger.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <exception>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace redis {
namespace {
namespace fs = std::filesystem;
using persistence::PersistError;
using persistence::PersistKind;

using std::chrono_literals::operator""s;

constexpr std::string_view MANIFEST_NAME = "appendonly.manifest";
/// Held for as long as a server uses the directory; see AppendOnlyFile::load.
constexpr std::string_view LOCK_NAME = "appendonly.lock";

/// Take the exclusive lock on \p dir that keeps a second server out of it. The
/// lock lives as long as the returned DurableFile is open, and goes with the
/// process if it dies, so a crash never leaves the directory locked.
std::expected<os::DurableFile, PersistError>
lock_directory(const fs::path &dir) {
	const fs::path path = dir / LOCK_NAME;
	auto lock           = os::DurableFile::open_append(path);
	if (!lock)
		return std::unexpected(PersistError{.kind  = PersistKind::CannotOpen,
											.cause = lock.error(),
											.path  = path});
	if (const auto busy = lock->try_lock()) {
		if (busy == std::errc::resource_unavailable_try_again)
			return std::unexpected(
				PersistError{.kind = PersistKind::InUse, .path = dir});
		return std::unexpected(PersistError{.kind  = PersistKind::CannotLock,
											.cause = busy,
											.path  = path});
	}
	return std::move(*lock);
}

/// Make sure \p path exists, without keeping it open.
std::expected<void, PersistError> create_empty(const fs::path &path) {
	const auto created = os::DurableFile::open_append(path);
	if (!created)
		return std::unexpected(PersistError{.kind  = PersistKind::CannotCreate,
											.cause = created.error(),
											.path  = path});
	return {};
}

spdlog::logger &log() {
	return logging::logger_for(logging::channel::persistence);
}

std::string base_name(uint64_t seq) {
	return fmt::format("appendonly.{}.base.snap", seq);
}

std::string incr_name(uint64_t seq) {
	return fmt::format("appendonly.{}.incr.aof", seq);
}

// Appends \p args to \p out as the RESP array of bulk strings a client sends.
template <typename Range>
void encode_command(std::string &out, const Range &args) {
	const auto into = std::back_inserter(out);
	fmt::format_to(into, "*{}\r\n", std::size(args));
	for (const std::string_view arg : args)
		fmt::format_to(into, "${}\r\n{}\r\n", arg.size(), arg);
}

// Whether \p name is one the server itself would have written, appendonly.<n>
// followed by \p suffix. The manifest names files in the AOF directory and the
// server reads, truncates and deletes them, so a name must never reach outside
// it: only digits are allowed between the fixed parts, so no separator, "..",
// drive letter or absolute path can get through.
bool is_managed_name(std::string_view name, std::string_view suffix) {
	constexpr std::string_view PREFIX = "appendonly.";
	if (name.size() <= PREFIX.size() + suffix.size() ||
		!name.starts_with(PREFIX) || !name.ends_with(suffix))
		return false;
	const auto seq =
		name.substr(PREFIX.size(), name.size() - PREFIX.size() - suffix.size());
	return std::ranges::all_of(seq,
							   [](char c) { return c >= '0' && c <= '9'; });
}

} // namespace

AppendOnlyFile::AppendOnlyFile(AofConfig config,
							   std::filesystem::path snapshot_file)
	: config_(std::move(config)), snapshot_file_(std::move(snapshot_file)) {}

// Logs and swallows: nothing can be done about a failure while shutting down.
AppendOnlyFile::~AppendOnlyFile() { // NOLINT(bugprone-exception-escape)
	cancel_rewrite();
	close();
}

// ---- manifest -------------------------------------------------------------

std::expected<AppendOnlyFile::Manifest, PersistError>
AppendOnlyFile::read_manifest(const fs::path &file) {
	// What is wrong with this manifest; quoted is the text that is.
	const auto bad = [&file](PersistKind kind, std::string quoted = {}) {
		return std::unexpected(PersistError{.kind   = kind,
											.path   = file,
											.quoted = std::move(quoted)});
	};

	std::ifstream in(file);
	if (!in) return bad(PersistKind::CannotOpen);

	Manifest manifest;
	std::string key;
	std::string value;
	while (in >> key >> value) {
		if (key == "base") {
			if (!is_managed_name(value, ".base.snap"))
				return bad(PersistKind::ManifestBadBase, value);
			manifest.base = value;
		} else if (key == "incr") {
			if (!is_managed_name(value, ".incr.aof"))
				return bad(PersistKind::ManifestBadIncr, value);
			manifest.incr.push_back(value);
		} else if (key == "seq") {
			const auto [ptr, ec] = std::from_chars(value.data(),
												   value.data() + value.size(),
												   manifest.seq);
			if (ec != std::errc()) return bad(PersistKind::ManifestBadSeq, value);
		} else {
			return bad(PersistKind::ManifestUnknownEntry, key);
		}
	}
	if (manifest.incr.empty()) return bad(PersistKind::ManifestNoIncr);
	return manifest;
}

// Replaced as a whole (SaveFile), so a crash leaves the old manifest or the new
// one, never half of either.
std::expected<void, PersistError>
AppendOnlyFile::write_manifest(const Manifest &manifest) const {
	const fs::path file = path_of(MANIFEST_NAME);

	std::string text;
	const auto into = std::back_inserter(text);
	if (manifest.base) fmt::format_to(into, "base {}\n", *manifest.base);
	for (const auto &name : manifest.incr)
		fmt::format_to(into, "incr {}\n", name);
	fmt::format_to(into, "seq {}\n", manifest.seq);

	auto save = os::SaveFile::open(file, fs::path(file).concat(".tmp"));
	if (!save) return std::unexpected(persistence::to_persist_error(save.error()));
	if (auto wrote = save->write(text); !wrote)
		return std::unexpected(persistence::to_persist_error(wrote.error()));
	if (auto done = save->commit(); !done)
		return std::unexpected(persistence::to_persist_error(done.error()));
	return {};
}

std::vector<fs::path> AppendOnlyFile::files() const {
	std::vector<fs::path> all;
	if (manifest_.base) all.push_back(path_of(*manifest_.base));
	for (const auto &name : manifest_.incr) all.push_back(path_of(name));
	return all;
}

// ---- loading --------------------------------------------------------------

std::expected<void, PersistError>
AppendOnlyFile::replay(const fs::path &file, bool last, CommandContext &ctx,
					   const CommandHandler &handler, size_t &commands) {
	// What is wrong with this file.
	const auto bad = [&file](PersistKind kind, std::error_code cause = {},
							 std::uint64_t at = 0, std::int64_t code = 0) {
		return std::unexpected(PersistError{
			.kind = kind, .cause = cause, .path = file, .at = at, .code = code});
	};

	// Opened once: what is read, and what is cut below, is the same file
	// however the name changes meanwhile. Only the last file can have a torn
	// end, so only it is opened for cutting.
	auto opened = os::DurableFile::open_existing(file,
												 last ? os::Access::read_write
													  : os::Access::read);
	if (!opened) return bad(PersistKind::CannotOpen, opened.error());
	auto content = opened->read_all();
	if (!content) return bad(PersistKind::CannotRead, content.error());
	const std::string_view data = *content;

	std::vector<std::string_view> args;
	std::deque<std::string> owned;
	std::string reply;
	size_t pos   = 0;
	size_t whole = 0; // end of the last command that parsed completely

	while (pos < data.size()) {
		args.clear();
		owned.clear();
		const auto found = resp::parse_request(data, pos, args, owned);
		if (!found) {
			if (!resp::Parser::is_incomplete(found.error()))
				return bad(PersistKind::LogCorrupt,
						   {},
						   pos,
						   static_cast<std::int64_t>(found.error()));
			// The server died while writing the last command. Redis' default
			// too: drop the torn command and carry on, rather than refuse to
			// start over a few bytes. Anywhere but the end it is corruption.
			if (!last) return bad(PersistKind::LogTornInside);
			log().warn("{} ends in the middle of a command: cutting {} bytes",
					   file,
					   data.size() - whole);
			if (const auto cut = opened->truncate(whole))
				return bad(PersistKind::CannotTruncate, cut);
			return {};
		}
		whole = pos;
		if (*found != resp::RequestKind::Command) continue;
		reply.clear();
		handler.execute(ctx, Args{args}, reply);
		++commands;
	}
	return {};
}

std::expected<void, PersistError>
AppendOnlyFile::load(CommandContext &ctx, const CommandHandler &handler) {
	std::error_code ec;
	const bool created = fs::create_directories(config_.dir, ec);
	if (ec)
		return std::unexpected(PersistError{.kind  = PersistKind::CannotCreate,
											.cause = ec,
											.path  = config_.dir});
	// A directory the server makes is the server's alone. One it was pointed at
	// keeps the permissions its owner gave it.
	if (created) {
		fs::permissions(config_.dir,
						fs::perms::owner_all,
						fs::perm_options::replace,
						ec);
		if (ec)
			log().warn("Cannot make {} private: {}", config_.dir, ec.message());
	}

	// Before anything is read or written: from here on this server is the only
	// one using the directory, so what it checks about the files in it (is
	// there a manifest yet?) is still true when it acts on it.
	if (!lock_.is_open()) {
		auto lock = lock_directory(config_.dir);
		if (!lock) return std::unexpected(lock.error());
		lock_ = std::move(*lock);
	}

	Database &db        = ctx.db;
	const fs::path file = path_of(MANIFEST_NAME);

	if (!fs::exists(file)) {
		// First start with AOF. What the server had - the snapshot - becomes
		// the base, so turning AOF on does not start from an empty keyspace.
		manifest_.seq  = 1;
		manifest_.incr = {incr_name(1)};
		if (fs::exists(snapshot_file_)) {
			if (auto loaded = persistence::detail::load_snapshot(db, snapshot_file_);
				!loaded)
				return loaded;
			manifest_.base = base_name(1);
			if (auto saved =
					persistence::detail::save_snapshot(db, path_of(*manifest_.base));
				!saved)
				return saved;
			log().info("AOF started from snapshot {}: {} keys",
					   snapshot_file_,
					   db.size());
		}
		if (auto made = create_empty(path_of(manifest_.incr.back())); !made)
			return made;
		if (auto written = write_manifest(manifest_); !written) return written;
	} else {
		auto manifest = read_manifest(file);
		if (!manifest) return std::unexpected(manifest.error());
		manifest_ = std::move(*manifest);

		// What was replayed ran at an earlier time: nothing may expire until
		// it is all back, or a later command could find a key gone that its
		// original run still saw.
		if (manifest_.base) {
			if (auto loaded =
					persistence::detail::load_snapshot(db,
											   path_of(*manifest_.base),
											   /*keep_expired=*/true);
				!loaded)
				return loaded;
		}
		// After the snapshot: loading it replaces the whole keyspace, flags
		// included.
		db.set_expiry_enabled(false);
		size_t commands = 0;
		CommandContext replaying{ctx.db, ctx.persistence};
		for (size_t i = 0; i < manifest_.incr.size(); ++i) {
			// No exists() first: replay() opens the file and says why when it
			// cannot, which is the only check that cannot go stale.
			const fs::path part = path_of(manifest_.incr[i]);
			if (auto replayed = replay(part,
									   i + 1 == manifest_.incr.size(),
									   replaying,
									   handler,
									   commands);
				!replayed)
				return replayed;
		}
		db.set_expiry_enabled(true);
		log().info("AOF loaded: {} commands from {} file(s), {} keys",
				   commands,
				   manifest_.incr.size(),
				   db.size());
	}

	if (auto opened = open_for_append(); !opened) return opened;
	db.release_owner(); // the keyspace thread claims it again
	db.track_expired(true);
	ctx.aof = this;
	return {};
}

std::expected<void, PersistError> AppendOnlyFile::open_for_append() {
	incr_path_  = path_of(manifest_.incr.back());
	auto opened = os::DurableFile::open_append(incr_path_);
	if (!opened)
		return std::unexpected(
			PersistError{.kind  = PersistKind::CannotOpenForAppend,
						 .cause = opened.error(),
						 .path  = incr_path_});
	// Asked of the open file, not of the name: it is this file's end that
	// good_size_ stands for.
	const auto size = opened->size();
	if (!size)
		return std::unexpected(PersistError{.kind  = PersistKind::CannotSize,
											.cause = size.error(),
											.path  = incr_path_});
	good_size_ = *size;
	auto file  = std::make_shared<os::DurableFile>(std::move(*opened));
	{
		const std::scoped_lock lock(mutex_);
		out_ = std::move(file);
	}
	if (config_.fsync == FsyncPolicy::everysec && !syncer_.joinable())
		syncer_ =
			std::jthread([this](std::stop_token stop) { sync_loop(stop); });
	return {};
}

// ---- logging --------------------------------------------------------------

void AppendOnlyFile::replace(std::span<const std::string_view> args) {
	assert(replaced_.empty() && "replace() called twice for one command");
	encode_command(replaced_, args);
}

void AppendOnlyFile::log_expired(Database &db) {
	if (!db.has_expired()) return;
	db.drain_expired([this](std::string_view key) {
		encode_command(buf_, std::array<std::string_view, 2>{"DEL", key});
	});
}

void AppendOnlyFile::command_ran(Database &db, bool logged, Args args) {
	log_expired(db);
	if (!logged) {
		replaced_.clear();
		return;
	}
	if (replaced_.empty()) {
		encode_command(buf_, args);
	} else {
		buf_ += replaced_;
		replaced_.clear();
	}
}

void AppendOnlyFile::write_out() {
	if (buf_.empty() || !out_) return;
	if (const auto err = out_->write_all(buf_)) {
		// Disk full or the like. A short write left part of a command in the
		// file: cut it, so the retry (the buffer is kept) does not follow
		// garbage. Cut through the descriptor that was written to.
		log().error("AOF write to {} failed: {}", incr_path_, err.message());
		if (const auto cut = out_->truncate(good_size_))
			log().error("AOF cannot cut {} back to {} bytes: {}",
						incr_path_,
						good_size_,
						cut.message());
		// Cap the pending buffer so a sustained disk error cannot exhaust
		// memory.
		constexpr size_t MAX_PENDING = 64 * 1024 * 1024;
		if (buf_.size() > MAX_PENDING) {
			log().critical(
				"AOF pending buffer exceeded {}MB; discarding {} bytes",
				MAX_PENDING / (1024 * 1024),
				buf_.size());
			buf_.clear();
		}
		return;
	}
	good_size_ += buf_.size();
	buf_.clear();
	dirty_.store(true, std::memory_order_release);
}

void AppendOnlyFile::sync_now() {
	if (!out_ || !dirty_.exchange(false, std::memory_order_acq_rel)) return;
	if (const auto err = out_->sync()) {
		log().error("AOF fsync of {} failed: {}", incr_path_, err.message());
		dirty_.store(true, std::memory_order_release);
	}
}

void AppendOnlyFile::after_batch() {
	write_out();
	if (config_.fsync != FsyncPolicy::always) return;
	sync_now();
	if (dirty_.load(std::memory_order_acquire) || !buf_.empty()) {
		// Replies are about to be sent for commands that are not on disk. With
		// "always" that is the promise being broken: stop, like Redis.
		log().critical(
			"Can't persist the AOF with appendfsync always: exiting");
		logging::flush();
		std::abort();
	}
}

void AppendOnlyFile::sync_loop(std::stop_token stop) {
	std::unique_lock lock(mutex_);
	while (!stop.stop_requested()) {
		wake_.wait_for(lock, stop, 1s, [] { return false; });
		if (stop.stop_requested()) break;
		if (!dirty_.exchange(false, std::memory_order_acq_rel)) continue;
		// A reference of its own to the file, so a rewrite that moves on to the
		// next one cannot close it under the fsync. Syncing the descriptor also
		// syncs the file that was written, not whatever its name is by now.
		const auto file = out_;
		lock.unlock();
		if (file) {
			if (const auto err = file->sync()) {
				log().error("AOF fsync failed: {}", err.message());
				dirty_.store(true, std::memory_order_release);
			}
		}
		lock.lock();
	}
}

void AppendOnlyFile::close() {
	if (out_) {
		write_out();
		dirty_.store(true, std::memory_order_release);
		if (syncer_.joinable()) {
			syncer_.request_stop();
			syncer_.join();
		}
		sync_now();
		const std::scoped_lock lock(mutex_);
		out_.reset();
	}
	// Last: until here no other server can take the directory over.
	lock_.close();
}

// ---- rewriting ------------------------------------------------------------

std::expected<void, PersistError>
AppendOnlyFile::start_rewrite(const Database &db) {
	if (child_)
		return std::unexpected(
			PersistError{.kind = PersistKind::RewriteInProgress});

	// Everything so far belongs to the old files: land it there, then log to a
	// new file, which is what the new base will not contain.
	write_out();
	if (!buf_.empty())
		return std::unexpected(PersistError{.kind = PersistKind::CannotWriteLog});
	dirty_.store(true, std::memory_order_release);
	sync_now();

	Manifest next = manifest_;
	next.seq += 1;
	next.incr.push_back(incr_name(next.seq));
	if (auto made = create_empty(path_of(next.incr.back())); !made) return made;
	if (auto written = write_manifest(next); !written) return written;

	manifest_ = std::move(next);
	if (auto opened = open_for_append(); !opened) return opened;

	rewrite_base_      = base_name(manifest_.seq);
	rewrite_base_path_ = path_of(rewrite_base_);
	auto child         = os::fork_and_run(
		[&] { return persistence::detail::save_snapshot(db, rewrite_base_path_); });
	if (!child) {
		// Harmless: the manifest lists both incremental files, which replay as
		// one. The next rewrite will merge them.
		log().error("Can't fork for AOF rewrite: {}", child.error().message());
		return std::unexpected(PersistError{.kind  = PersistKind::CannotFork,
											.cause = child.error()});
	}
	child_.emplace(std::move(*child));
	log().info("Background AOF rewriting started by pid {}", child_->pid());
	return {};
}

void AppendOnlyFile::poll() {
	if (!child_) return;
	if (const auto done = os::reap(*child_, false)) finish_rewrite(*done);
}

void AppendOnlyFile::finish_rewrite(const os::ChildResult &result) {
	const os::pid_type pid = child_->pid();
	child_.reset();
	if (!result.success) {
		log().error("Background AOF rewrite failed: {}", result.error);
		std::error_code ignored;
		fs::remove(persistence::detail::snapshot_temp_path(rewrite_base_path_, pid),
				   ignored);
		return;
	}

	// The new base covers everything before the newest incremental file.
	Manifest next{.base = rewrite_base_,
				  .incr = {manifest_.incr.back()},
				  .seq  = manifest_.seq};
	if (auto written = write_manifest(next); !written) {
		log().error("Background AOF rewrite finished but {}", written.error());
		return;
	}
	std::vector<fs::path> old;
	if (manifest_.base) old.push_back(path_of(*manifest_.base));
	for (size_t i = 0; i + 1 < manifest_.incr.size(); ++i)
		old.push_back(path_of(manifest_.incr[i]));
	manifest_ = std::move(next);
	std::error_code ignored;
	for (const fs::path &file : old) fs::remove(file, ignored);
	log().info("Background AOF rewrite finished successfully");
}

void AppendOnlyFile::cancel_rewrite() {
	if (!child_) return;
	const os::pid_type pid = child_->pid();
	log().warn("Killing the AOF rewrite child {}", pid);
	os::kill_child(pid);
	(void)os::reap(*child_, true);
	child_.reset();
	std::error_code ignored;
	fs::remove(persistence::detail::snapshot_temp_path(rewrite_base_path_, pid),
			   ignored);
}

[[nodiscard]] bool AppendOnlyFile::rewrite_in_progress() const noexcept {
	return child_.has_value();
}

[[nodiscard]] FsyncPolicy AppendOnlyFile::fsync_policy() const noexcept {
	return config_.fsync;
}


} // namespace redis
