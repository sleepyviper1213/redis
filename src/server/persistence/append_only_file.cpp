#include "append_only_file.hpp"

#include "core/logging.hpp"
#include "core/resp/parser.hpp"
#include "core/resp/request.hpp"
#include "server/commands/handler.hpp"
#include "server/database.hpp" // IWYU pragma: keep (db.drain_expired())
#include "server/persistence/snapshot.hpp"

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
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace redis {
namespace {
namespace fs = std::filesystem;
namespace px = persistence::posix;

using std::chrono_literals::operator""s;

constexpr std::string_view MANIFEST_NAME = "appendonly.manifest";

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

std::expected<std::string, std::string> read_file(const fs::path &file) {
	std::ifstream in(file, std::ios::binary);
	if (!in) return std::unexpected(fmt::format("cannot open {}", file));
	std::ostringstream content;
	content << in.rdbuf();
	return std::move(content).str();
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

std::expected<AppendOnlyFile::Manifest, std::string>
AppendOnlyFile::read_manifest(const fs::path &file) {
	std::ifstream in(file);
	if (!in) return std::unexpected(fmt::format("cannot open {}", file));

	Manifest manifest;
	std::string key;
	std::string value;
	while (in >> key >> value) {
		if (key == "base") {
			if (!is_managed_name(value, ".base.snap"))
				return std::unexpected(
					fmt::format("{}: bad base file name '{}'", file, value));
			manifest.base = value;
		} else if (key == "incr") {
			if (!is_managed_name(value, ".incr.aof"))
				return std::unexpected(
					fmt::format("{}: bad incremental file name '{}'",
								file,
								value));
			manifest.incr.push_back(value);
		} else if (key == "seq") {
			const auto [ptr, ec] = std::from_chars(value.data(),
												   value.data() + value.size(),
												   manifest.seq);
			if (ec != std::errc()) {
				return std::unexpected(
					fmt::format("{}: bad seq '{}'", file, value));
			}
		} else {
			return std::unexpected(
				fmt::format("{}: unknown entry '{}'", file, key));
		}
	}
	if (manifest.incr.empty())
		return std::unexpected(
			fmt::format("{} lists no incremental file", file));
	return manifest;
}

// Written beside the manifest and renamed over it, so a crash leaves the old
// manifest or the new one, never half of either.
std::expected<void, std::string>
AppendOnlyFile::write_manifest(const Manifest &manifest) const {
	const fs::path file = path_of(std::string(MANIFEST_NAME));
	const fs::path tmp  = fs::path(file).concat(".tmp");
	// Names the step that failed and leaves no temporary file behind.
	const auto failed = [&](std::string_view step, const std::error_code &why) {
		std::error_code ignored;
		fs::remove(tmp, ignored);
		return std::unexpected(fmt::format("cannot write {}: {} failed{}{}",
										   file,
										   step,
										   why ? ": " : "",
										   why ? why.message() : ""));
	};

	bool written = false;
	{
		std::ofstream out(tmp, std::ios::trunc);
		if (manifest.base) out << "base " << *manifest.base << '\n';
		for (const auto &name : manifest.incr) out << "incr " << name << '\n';
		out << "seq " << manifest.seq << '\n';
		out.flush();
		written = static_cast<bool>(out);
	} // closed before it can be removed
	if (!written) return failed("write", {});
	if (const auto why = px::sync_file(tmp)) return failed("fsync", why);

	std::error_code why;
	fs::rename(tmp, file, why);
	if (why) return failed("rename", why);
	if (why = px::sync_directory(config_.dir); why)
		return failed("fsync of the directory", why);
	return {};
}

std::vector<fs::path> AppendOnlyFile::files() const {
	std::vector<fs::path> all;
	if (manifest_.base) all.push_back(path_of(*manifest_.base));
	for (const auto &name : manifest_.incr) all.push_back(path_of(name));
	return all;
}

// ---- loading --------------------------------------------------------------

std::expected<void, std::string>
AppendOnlyFile::replay(const fs::path &file, bool last, CommandContext &ctx,
					   const CommandHandler &handler, size_t &commands) {
	auto content = read_file(file);
	if (!content) return std::unexpected(content.error());
	const std::string &data = *content;

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
				return std::unexpected(
					fmt::format("{} is corrupt at byte {}: {}",
								file,
								pos,
								found.error()));
			// The server died while writing the last command. Redis' default
			// too: drop the torn command and carry on, rather than refuse to
			// start over a few bytes. Anywhere but the end it is corruption.
			if (!last)
				return std::unexpected(
					fmt::format("{} ends in the middle of a command", file));
			log().warn("{} ends in the middle of a command: cutting {} bytes",
					   file,
					   data.size() - whole);
			std::error_code ec;
			fs::resize_file(file, whole, ec);
			if (ec)
				return std::unexpected(
					fmt::format("cannot truncate {}: {}", file, ec.message()));
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

std::expected<void, std::string>
AppendOnlyFile::load(CommandContext &ctx, const CommandHandler &handler) {
	std::error_code ec;
	fs::create_directories(config_.dir, ec);
	if (ec)
		return std::unexpected(
			fmt::format("cannot create {}: {}", config_.dir, ec.message()));

	Database &db        = ctx.db;
	const fs::path file = path_of(std::string(MANIFEST_NAME));

	if (!fs::exists(file)) {
		// First start with AOF. What the server had - the snapshot - becomes
		// the base, so turning AOF on does not start from an empty keyspace.
		manifest_.seq  = 1;
		manifest_.incr = {incr_name(1)};
		if (fs::exists(snapshot_file_)) {
			if (auto loaded = persistence::load_snapshot(db, snapshot_file_);
				!loaded)
				return loaded;
			manifest_.base = base_name(1);
			if (auto saved =
					persistence::save_snapshot(db, path_of(*manifest_.base));
				!saved)
				return saved;
			log().info("AOF started from snapshot {}: {} keys",
					   snapshot_file_,
					   db.size());
		}
		std::ofstream(path_of(manifest_.incr.back()), std::ios::app).close();
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
					persistence::load_snapshot(db,
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
			const fs::path part = path_of(manifest_.incr[i]);
			if (!fs::exists(part))
				return std::unexpected(
					fmt::format("{} is listed in the manifest but missing",
								part));
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

std::expected<void, std::string> AppendOnlyFile::open_for_append() {
	incr_path_ = path_of(manifest_.incr.back());
	out_.close();
	out_.clear();
	out_.open(incr_path_, std::ios::binary | std::ios::app);
	if (!out_)
		return std::unexpected(
			fmt::format("cannot open {} for appending", incr_path_));
	std::error_code ec;
	good_size_ = fs::file_size(incr_path_, ec);
	{
		const std::scoped_lock lock(mutex_);
		sync_path_ = incr_path_;
	}
	if (config_.fsync == FsyncPolicy::everysec && !syncer_.joinable())
		syncer_ =
			std::jthread([this](std::stop_token stop) { sync_loop(stop); });
	return {};
}

// ---- logging --------------------------------------------------------------

void AppendOnlyFile::replace(std::initializer_list<std::string_view> args) {
	assert(replaced_.empty() && "replace() called twice for one command");
	encode_command(replaced_, args);
}

void AppendOnlyFile::replace(const std::vector<std::string_view> &args) {
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
	if (buf_.empty()) return;
	out_.write(buf_.data(), static_cast<std::streamsize>(buf_.size()));
	out_.flush();
	if (!out_) {
		// Disk full or the like. A short write left part of a command in the
		// file: cut it, so the retry (the buffer is kept) does not follow
		// garbage.
		log().error("AOF write to {} failed", incr_path_);
		out_.clear();
		std::error_code ec;
		fs::resize_file(incr_path_, good_size_, ec);
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
	if (!dirty_.exchange(false, std::memory_order_acq_rel)) return;
	if (auto ec = px::sync_file(incr_path_)) {
		log().error("AOF fsync of {} failed: {}", incr_path_, ec.message());
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
		const fs::path path = sync_path_;
		lock.unlock();
		if (auto ec = px::sync_file(path)) {
			log().error("AOF fsync of {} failed: {}", path, ec.message());
			dirty_.store(true, std::memory_order_release);
		}
		lock.lock();
	}
}

void AppendOnlyFile::close() {
	if (!out_.is_open()) return;
	write_out();
	dirty_.store(true, std::memory_order_release);
	if (syncer_.joinable()) {
		syncer_.request_stop();
		syncer_.join();
	}
	sync_now();
	out_.close();
}

// ---- rewriting ------------------------------------------------------------

std::expected<void, std::string>
AppendOnlyFile::start_rewrite(const Database &db) {
	if (child_)
		return std::unexpected(
			"ERR Background append only file rewriting already in progress");

	// Everything so far belongs to the old files: land it there, then log to a
	// new file, which is what the new base will not contain.
	write_out();
	if (!buf_.empty()) return std::unexpected("ERR cannot write the AOF");
	dirty_.store(true, std::memory_order_release);
	sync_now();

	Manifest next = manifest_;
	next.seq += 1;
	next.incr.push_back(incr_name(next.seq));
	std::ofstream(path_of(next.incr.back()), std::ios::app).close();
	if (auto written = write_manifest(next); !written)
		return std::unexpected("ERR " + written.error());

	manifest_ = std::move(next);
	if (auto opened = open_for_append(); !opened)
		return std::unexpected("ERR " + opened.error());

	rewrite_base_      = base_name(manifest_.seq);
	rewrite_base_path_ = path_of(rewrite_base_);
	auto child         = px::fork_and_run(
		[&] { return persistence::save_snapshot(db, rewrite_base_path_); });
	if (!child) {
		// Harmless: the manifest lists both incremental files, which replay as
		// one. The next rewrite will merge them.
		log().error("Can't fork for AOF rewrite: {}", child.error().message());
		return std::unexpected("ERR Can't fork: " + child.error().message());
	}
	child_.emplace(std::move(*child));
	log().info("Background AOF rewriting started by pid {}", child_->pid());
	return {};
}

void AppendOnlyFile::poll() {
	if (!child_) return;
	if (const auto done = px::reap(*child_, false)) finish_rewrite(*done);
}

void AppendOnlyFile::finish_rewrite(const px::ChildResult &result) {
	const px::pid_type pid = child_->pid();
	child_.reset();
	if (!result.success) {
		log().error("Background AOF rewrite failed: {}", result.error);
		std::error_code ignored;
		fs::remove(persistence::snapshot_temp_path(rewrite_base_path_, pid),
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
	const px::pid_type pid = child_->pid();
	log().warn("Killing the AOF rewrite child {}", pid);
	px::kill_child(pid);
	(void)px::reap(*child_, true);
	child_.reset();
	std::error_code ignored;
	fs::remove(persistence::snapshot_temp_path(rewrite_base_path_, pid),
			   ignored);
}

[[nodiscard]] bool AppendOnlyFile::rewrite_in_progress() const noexcept {
	return child_.has_value();
}

[[nodiscard]] FsyncPolicy AppendOnlyFile::fsync_policy() const noexcept {
	return config_.fsync;
}


} // namespace redis
