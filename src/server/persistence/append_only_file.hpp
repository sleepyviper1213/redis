#pragma once
#include "server/commands/command.hpp"
#include "server/persistence/child_process.hpp"
#include "server/database_fwd.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace redis {
class CommandHandler;

/**
 * \brief When the log is forced to disk, as Redis' appendfsync.
 *
 * The enumerators are spelled as the option values, so magic_enum converts
 * between them and the command line and CONFIG GET.
 */
enum class FsyncPolicy : uint8_t {
	always,   ///< before the replies of a batch are sent: nothing acknowledged is lost
	everysec, ///< once a second, off the command thread: at most ~1s is lost
	no,       ///< never; the operating system decides
};

struct AofConfig {
	/// Holds the manifest, the base snapshot and the incremental logs.
	std::filesystem::path dir = "appendonlydir";
	FsyncPolicy fsync         = FsyncPolicy::everysec;
};

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
	[[nodiscard]] std::expected<void, std::string>
	load(CommandContext &ctx, const CommandHandler &handler);

	/**
	 * \brief Called after every command: log the keys that expired meanwhile
	 * (as DEL, before the command, which may have found them gone), then, if
	 * \p logged, the command itself - or what replace() set in its place.
	 */
	void command_ran(Database &db, bool logged, Args args);

	/// Log \p args instead of the command's own arguments. Call from a command
	/// while it runs, and only when `ctx.aof` is set.
	void replace(std::initializer_list<std::string_view> args);
	void replace(const std::vector<std::string_view> &args);

	/// Log the keys that expired since the last call (the periodic tick).
	void log_expired(Database &db);

	/// Write what is buffered to the file; with FsyncPolicy::always, sync it.
	/// Called once per batch, before its replies go out.
	void after_batch();

	/// Write out, sync, and stop logging. For shutdown.
	void close();

	/**
	 * \brief Start rewriting the log into a snapshot (BGREWRITEAOF).
	 * \return An error message, in reply form, if one is already running or it
	 *         cannot start.
	 */
	[[nodiscard]] std::expected<void, std::string>
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

	[[nodiscard]] std::expected<void, std::string>
	replay(const std::filesystem::path &file, bool last, CommandContext &ctx,
		   const CommandHandler &handler, size_t &commands);

	[[nodiscard]] std::expected<void, std::string> open_for_append();

	void write_out();
	void sync_now();
	void sync_loop(std::stop_token stop);
	void finish_rewrite(const persistence::posix::ChildResult &result);

	[[nodiscard]] std::filesystem::path path_of(const std::string &name) const {
		return config_.dir / name;
	}

	[[nodiscard]] static std::expected<Manifest, std::string>
	read_manifest(const std::filesystem::path &file);

	[[nodiscard]] std::expected<void, std::string>
	write_manifest(const Manifest &manifest) const;

	AofConfig config_;
	std::filesystem::path snapshot_file_;
	Manifest manifest_;

	std::string buf_;      ///< logged but not yet written
	std::string replaced_; ///< what replace() set, until command_ran() takes it
	std::ofstream out_;
	std::filesystem::path incr_path_;
	uintmax_t good_size_ =
		0; ///< bytes of incr_path_ known to be whole commands

	// The once-a-second sync: dirty_ says there is something to sync, and
	// sync_path_ (under mutex_) which file.
	std::atomic<bool> dirty_{false};
	std::mutex mutex_;
	std::condition_variable_any wake_;
	std::filesystem::path sync_path_;
	std::jthread syncer_;

	std::optional<persistence::posix::Child> child_;
	std::string rewrite_base_;
	std::filesystem::path rewrite_base_path_;
};

} // namespace redis
