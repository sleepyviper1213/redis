#pragma once
#include "core/stdexec_ext/task.hpp"
#include "diagnostics/command_watchdog.hpp"
#include "net/net_limits.hpp"
#include "persistence/aof_config.hpp"
#include "server_error.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace redis {
namespace net = boost::asio;

struct Services;

struct ServerConfig {
	std::string host                = "127.0.0.1";
	uint16_t port                   = 6379;
	std::filesystem::path dump_file = "dump.snap";
	/// Threads running network I/O. 1 runs everything on one thread; more
	/// adds a thread dedicated to executing commands (see Executor).
	unsigned io_threads = 1;
	/// Log write commands to an append-only file; unset, snapshots alone
	/// persist the keyspace.
	std::optional<AofConfig> aof;
	/// Client count, buffer and idle limits.
	NetLimits limits;
	/// Let clients change settings with CONFIG SET.
	bool is_config_set_enabled = false;
	/// Report a command that holds up the keyspace thread too long.
	diagnostics::CommandWatchdogSettings watchdog;
	/// Allow only the system calls the server needs once it serves (ADR-0014;
	/// Linux). main() confines its files; see os::confine_paths.
	bool is_sandboxed = false;
	/// Test seam, not a command line option: housekeeping fails after this many
	/// ticks, to exercise the FailFast path (TC-262). Unset in production.
	std::optional<unsigned> fail_housekeeping_after;
};

/**
 * \brief The composition root: owns the keyspace, persistence, the command
 * table and the event loop.
 *
 * Commands execute one at a time on a single thread, like Redis, so the
 * keyspace needs no locks. With ServerConfig::io_threads == 1 that is the one
 * thread that also does the I/O; with more, it is a dedicated thread and the
 * I/O threads only read, parse and write.
 */
class RedisServer {
	/// Only create() can make one (docs/design/patterns.md).
	struct Token {
		explicit Token() = default;
	};

public:
	/**
	 * rief A server ready to run(), or why not: Asio could not set up its
	 * event loop or the SIGINT / SIGTERM handling (it reports that by
	 * throwing). Not movable, so handed out by pointer, never null.
	 */
	[[nodiscard]] static std::expected<std::unique_ptr<RedisServer>,
									   ServerError>
	create(ServerConfig config);

	/// For create() only; throws what Asio throws.
	RedisServer(Token, ServerConfig config);

	RedisServer(const RedisServer &)            = delete;
	RedisServer &operator=(const RedisServer &) = delete;
	RedisServer(RedisServer &&)                 = delete;
	RedisServer &operator=(RedisServer &&)      = delete;
	~RedisServer()                              = default;

	/**
	 * \brief Load the snapshot, serve clients until SIGINT or SIGTERM, then
	 * save a final snapshot. Blocks.
	 * \return Why the server could not start or ended abnormally: the files it
	 *         persists to cannot be read, an I/O thread failed, or the final
	 *         snapshot could not be written.
	 */
	[[nodiscard]] std::expected<void, ServerError> run();

private:
	/// Periodic housekeeping, like Redis' serverCron: active expiry and
	/// collecting finished background saves. Runs until its scope is stopped.
	stdexec_ext::InlineTask<void> cron();

	/// What runs beside the clients for as long as they are served: cron()
	/// and the watchdog. Ends when its scope is stopped, or when either fails.
	stdexec_ext::InlineTask<void> housekeeping();

	/// How often cron() runs, and how many keys one expiry cycle may remove.
	static constexpr auto CRON_PERIOD = std::chrono::milliseconds(100);
	static constexpr size_t EXPIRE_KEYS_PER_RUN = 1000;

	ServerConfig config_;
	net::io_context io_;
	net::signal_set signals_;
	/// Reports a command holding up the keyspace thread; see housekeeping().
	diagnostics::CommandWatchdog command_watchdog_;

	/// Shared with the front end and every session; see Services.
	std::shared_ptr<Services> services_;
};
} // namespace redis
