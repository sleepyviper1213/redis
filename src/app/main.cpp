#include "server/diagnostics/crash.hpp"
#include "server/log_channels.hpp"
#include "server/redis_server.hpp"
#include "setup_cli_options.hpp"

#include <CLI/CLI.hpp>
#include <fmt/base.h>
#include <magic_enum/magic_enum.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <optional>
#include <string>

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
		.fsync = magic_enum::enum_cast<redis::FsyncPolicy>(
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

	try {
		redis::RedisServer server(redis::ServerConfig{
			.host       = app->get_option("--host")->as<std::string>(),
			.port       = app->get_option("--port")->as<uint16_t>(),
			.dump_file  = app->get_option("--dump")->as<std::string>(),
			.io_threads = app->get_option("--io-threads")->as<unsigned>(),
			.aof        = aof_config(*app),
			.limits     = net_limits(*app),
			.config_set_enabled =
				app->get_option("--enable-config-set")->as<bool>(),
			.watchdog = {.threshold = std::chrono::milliseconds(
							 app->get_option("--watchdog-ms")->as<int64_t>())},
		});
		if (const auto ran = server.run(); !ran) {
			log.critical("{}", ran.error());
			return EXIT_FAILURE;
		}
	} catch (const std::exception &e) {
		// Only what Asio reports by throwing, e.g. boost::system::system_error
		// when the port is already in use.
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
