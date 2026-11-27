#include "core/os/sandbox.hpp"
#include "core/utils/enum_name.hpp"
#include "server/diagnostics/crash.hpp"
#include "server/log_channels.hpp"
#include "server/persistence/append_only_file.hpp"
#include "server/redis_server.hpp"
#include "setup_cli_options.hpp"

#include <CLI/CLI.hpp>
#include <fmt/base.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {
int current_pid() {
#ifdef _WIN32
	return _getpid();
#else
	return static_cast<int>(getpid());
#endif
}

// The append-only file the options ask for, if any.
std::optional<redis::AofConfig> aof_config(const CLI::App &app) {
	if (!app.get_option("--appendonly")->as<bool>()) return std::nullopt;
	return redis::AofConfig{
		.dir = app.get_option("--appenddir")->as<std::string>(),
		// The option only accepts the enumerators' names.
		.fsync = redis::enum_cast<redis::FsyncPolicy>(
					 app.get_option("--appendfsync")->as<std::string>())
					 .value_or(redis::FsyncPolicy::everysec),
	};
}

redis::NetLimits net_limits(const CLI::App &app) {
	return redis::NetLimits{
		.max_clients = app.get_option("--maxclients")->as<size_t>(),
		.max_query_buffer =
			app.get_option("--client-query-buffer-limit")->as<size_t>(),
		.max_output_buffer =
			app.get_option("--client-output-buffer-limit")->as<size_t>(),
		.idle_timeout = std::chrono::seconds(
			app.get_option("--idle-timeout")->as<int64_t>()),
		.request_timeout = std::chrono::seconds(
			app.get_option("--request-timeout")->as<int64_t>()),
	};
}

/// Test seam for TC-262: REDIS_TEST_FAIL_HOUSEKEEPING_AFTER=<ticks> makes
/// housekeeping fail after that many ticks. Not a documented option.
std::optional<unsigned> test_fail_housekeeping_after() {
	const char *env = std::getenv("REDIS_TEST_FAIL_HOUSEKEEPING_AFTER"); // NOLINT(concurrency-mt-unsafe): read before any thread starts
	if (env == nullptr) return std::nullopt;
	return static_cast<unsigned>(std::strtoul(env, nullptr, 10));
}

/// The directory \p file is in, absolute: "redis_server.log" is in the
/// working directory, not in "".
std::filesystem::path directory_of(const std::filesystem::path &file) {
	std::error_code ec;
	const auto absolute = std::filesystem::absolute(file, ec);
	return (ec ? file : absolute).parent_path();
}

/**
 * rief Confine the files every later thread can open to the server's own
 * (ADR-0014): the dump file's, the append-only and the log file's
 * directories, and the time zone, read only.
 *
 * Before the server starts its threads (the keyspace, the I/O): Landlock
 * covers only the calling thread and those it starts. The log writer, already
 * running, holds only the log file.
 */
std::expected<redis::os::Confinement, std::string>
confine_files(const CLI::App &app) {
	std::vector<std::filesystem::path> read_write{
		directory_of(app.get_option("--dump")->as<std::string>()),
		directory_of(app.get_option("--log-file")->as<std::string>()),
	};
	if (const auto aof = aof_config(app)) {
		// Landlock allows only what exists: make it now, as load() would.
		if (auto made = redis::AppendOnlyFile::create_directory(aof->dir); !made)
			return std::unexpected(display(made.error()));
		read_write.push_back(aof->dir);
	}
	const std::array<std::filesystem::path, 2> read_only{
		"/etc/localtime", "/usr/share/zoneinfo"};

	auto confined = redis::os::confine_paths(
		{.read_write = read_write, .read_only = read_only});
	if (!confined) return std::unexpected(display(confined.error()));
	return *confined;
}

int run(int argc, char **argv) {
	const auto app = redis::setup_cli_options();
	CLI11_PARSE(*app, argc, argv);

	// Shuts logging down (joining the async writer) on every return path.
	const redis::logging::guard log_guard{{
		.level    = app->get_option("--log-level")->as<std::string>(),
		.log_file = app->get_option("--log-file")->as<std::string>(),
		.channels = redis::logging::channel_names(),
	}};
	auto &log = redis::logging::logger_for(redis::logging::channel::server);
	redis::diagnostics::install_crash_handler(
		app->get_option("--log-file")->as<std::string>());

	// Who was running, and how: what an operator compares against a crash
	// report or a core dump.
	log.info("Server started, pid {}, port {}, {} I/O thread(s), dump file {}",
			 current_pid(),
			 app->get_option("--port")->as<uint16_t>(),
			 app->get_option("--io-threads")->as<unsigned>(),
			 app->get_option("--dump")->as<std::string>());

	if (app->count("--config") != 0u)
		log.info("Using config file: {}",
				 app->get_option("--config")->as<std::string>());

	const bool is_sandboxed =
		app->get_option("--sandbox")->as<std::string>() == "yes";
	if (is_sandboxed) {
		const auto confined = confine_files(*app);
		if (!confined) {
			log.critical("{} (--sandbox no to start without it)",
						 confined.error());
			return EXIT_FAILURE;
		}
		log.info("Sandbox: files {}", *confined);
	} else {
		log.warn("Sandbox off (--sandbox no)");
	}

	try {
		auto server = redis::RedisServer::create(redis::ServerConfig{
			.host       = app->get_option("--host")->as<std::string>(),
			.port       = app->get_option("--port")->as<uint16_t>(),
			.dump_file  = app->get_option("--dump")->as<std::string>(),
			.io_threads = app->get_option("--io-threads")->as<unsigned>(),
			.aof        = aof_config(*app),
			.limits     = net_limits(*app),
			.is_config_set_enabled =
				app->get_option("--enable-config-set")->as<bool>(),
			.watchdog = {.threshold = std::chrono::milliseconds(
							 app->get_option("--watchdog-ms")->as<int64_t>())},
			.is_sandboxed            = is_sandboxed,
			.fail_housekeeping_after = test_fail_housekeeping_after(),
		});
		if (!server) {
			log.critical("{}", server.error());
			return EXIT_FAILURE;
		}
		if (const auto ran = (*server)->run(); !ran) {
			log.critical("{}", ran.error());
			return EXIT_FAILURE;
		}
	} catch (const std::exception &e) {
		// Not expected: setup and run() report their failures as values. This
		// keeps an exception that escapes anyway from ending the process
		// without a log line.
		log.critical("{}", e.what());
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
} // namespace

int main(int argc, char **argv) {
	// Whatever fails before logging is up (options, log file) has nowhere to
	// be logged: report it on stderr instead of letting it end the process.
	try {
		return run(argc, argv);
	} catch (const std::exception &e) {
		fmt::println(stderr, "fatal: {}", e.what());
		return EXIT_FAILURE;
	}
}
