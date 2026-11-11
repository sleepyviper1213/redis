#pragma once
#include "core/os/child_process.hpp"
#include "core/os/durable_file.hpp"
#include "persist_error.hpp"
#include "aof_config.hpp"
#include "common/store/database_fwd.hpp"
#include "server/commands/args.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace redis {
class CommandHandler;
struct CommandContext;

/**
 * \brief The append-only file: every command that changes the keyspace, in
 * order, as the RESP the client sent, so replaying the file rebuilds it.
 *
 * Laid out like Redis 7's multi-part AOF, in a directory:
 *
 *   appendonly.manifest          which files make up the log
 *   appendonly.<n>.base.snap     a snapshot (this project's format)
 *   appendonly.<n>.incr.aof      commands since the base, or since the
 *                                previous incremental file
 *
 * The manifest lists at most one base and the incremental files in order. The
 * state is the base plus the replay of each incremental file.
 *
 * BGREWRITEAOF compacts it without pausing the server: it starts a new
 * incremental file and forks a child that writes a snapshot of the keyspace as
 * of the fork, the new base. Everything after the fork is in the new
 * incremental file, so nothing needs to be buffered while the child works.
 * Only once the child has succeeded does the manifest switch to
 * {new base, new incremental}; until then it lists the old files too, so a
 * crash or a failed child loses nothing.
 *
 * Commands that cannot be replayed as received (they depend on the clock or on
 * chance) are logged in a form that can; see replace(). Keys that expire are
 * logged as DEL, and expiry is switched off while replaying, so the replay
 * does not depend on when it runs. @see Database::track_expired
 *
 * The directory belongs to one server at a time: load() takes an exclusive
 * lock on appendonly.lock and holds it until close(), or until the process ends
 * however it ends, so a second server pointed at the same directory refuses to
 * start instead of racing the first one for the manifest and the log. The log
 * is written, sized, truncated and synced through the descriptor it was opened
 * with, never by its name again, and the files are created private to the owner
 * (the directory too, when the server creates it).
 *
 * Used by the keyspace thread only, except the once-a-second fsync, which
 * runs on a thread of its own and touches nothing but the file.
 */
class AppendOnlyFile {
public:
	AppendOnlyFile(AofConfig config, std::filesystem::path snapshot_file);

	AppendOnlyFile(const AppendOnlyFile &)            = delete;
	AppendOnlyFile &operator=(const AppendOnlyFile &) = delete;

	/// Writes out and syncs what is buffered, and abandons a rewrite in
	/// progress.
	~AppendOnlyFile();

	/**
	 * \brief Bring \p ctx.db up to date from the directory, then log to it.
	 *
	 * With no manifest yet this is the first start with AOF: the snapshot file
	 * the server was using, if any, is loaded and becomes the base. Otherwise
	 * the base is loaded and the incremental files replayed through
	 * \p handler. A torn last command (the server died mid-write) is cut off
	 * with a warning; a file that does not parse is an error and the server
	 * should not start.
	 *
	 * On success \p ctx.aof is this file, and the keyspace tracks its expired
	 * keys. Call on the thread that will start the server; the keyspace is
	 * handed over afterwards.
	 */
	[[nodiscard]] std::expected<void, persistence::PersistError>
	load(CommandContext &ctx, const CommandHandler &handler);

	/**
	 * \brief Called after every command: log the keys that expired meanwhile
	 * (as DEL, before the command, which may have found them gone), then, if
	 * \p logged, the command itself - or what replace() set in its place.
	 */
	void command_ran(Database &db, bool logged, Args args);

	/// Log \p args instead of the command's own arguments. Call from a command
	/// while it runs, and only when `ctx.aof` is set.
	void replace(std::span<const std::string_view> args);

	/// For a literal list, `replace({"DEL", key})`: the length is deduced.
	template <size_t N>
	void replace(const std::string_view (&args)[N]) {
		replace(std::span<const std::string_view, N>(args));
	}

	/// Log the keys that expired since the last call (the periodic tick).
	void log_expired(Database &db);

	/// Write what is buffered to the file; with FsyncPolicy::always, sync it.
	/// Called once per batch, before its replies go out.
	void after_batch();

	/// Write out, sync, and stop logging. For shutdown.
	void close();

	/**
	 * \brief Start rewriting the log into a snapshot (BGREWRITEAOF).
	 * \return Why not, if one is already running or it cannot start.
	 */
	[[nodiscard]] std::expected<void, persistence::PersistError>
	start_rewrite(const Database &db);

	/// Collect a finished rewrite, if any. Never blocks.
	void poll();

	/// Kill a running rewrite and clean up after it.
	void cancel_rewrite();

	[[nodiscard]] bool rewrite_in_progress() const noexcept;

	[[nodiscard]] FsyncPolicy fsync_policy() const noexcept;

	/// The files the manifest lists, base first. For tests and diagnostics.
	[[nodiscard]] std::vector<std::filesystem::path> files() const;

private:
	struct Manifest {
		std::optional<std::string> base;
		std::vector<std::string> incr;
		uint64_t seq = 0; ///< the highest number used in a file name
	};

	[[nodiscard]] static std::expected<void, persistence::PersistError>
	replay(const std::filesystem::path &file, bool last, CommandContext &ctx,
		   const CommandHandler &handler, size_t &commands);

	[[nodiscard]] std::expected<void, persistence::PersistError>
	open_for_append();

	void write_out();
	void sync_now();
	void sync_loop(const std::stop_token &stop);
	void finish_rewrite(const os::ChildResult &result);

	[[nodiscard]] std::filesystem::path path_of(std::string_view name) const {
		return config_.dir / name;
	}

	[[nodiscard]] static std::expected<Manifest, persistence::PersistError>
	read_manifest(const std::filesystem::path &file);

	[[nodiscard]] std::expected<void, persistence::PersistError>
	write_manifest(const Manifest &manifest) const;

	AofConfig config_;
	std::filesystem::path snapshot_file_;
	Manifest manifest_;

	/// Holds the directory's lock, from load() to close().
	os::DurableFile lock_;

	std::string buf_;      ///< logged but not yet written
	std::string replaced_; ///< what replace() set, until command_ran() takes it
	/// The incremental file being appended to. Shared with the sync thread,
	/// which keeps it open while it syncs even if a rewrite has moved on to the
	/// next one. Replaced only under mutex_, and only by the keyspace thread.
	std::shared_ptr<os::DurableFile> out_;
	std::filesystem::path incr_path_; ///< out_'s name, for messages
	uintmax_t good_size_ = 0; ///< bytes of out_ known to be whole commands

	// The once-a-second sync: dirty_ says there is something to sync; the file
	// is out_, read under mutex_.
	std::atomic<bool> dirty_{false};
	std::mutex mutex_;
	std::condition_variable_any wake_;
	std::jthread syncer_;

	std::optional<os::Child> child_;
	std::string rewrite_base_;
	std::filesystem::path rewrite_base_path_;
};

} // namespace redis
