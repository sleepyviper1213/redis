#include "setup_cli_options.hpp"

#include "server/persistence/append_only_file.hpp"

#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <magic_enum/magic_enum.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace redis {
namespace {
struct MyFormatter : public CLI::Formatter {
	std::string make_option_opts(const CLI::Option *opt) const override {
		if (opt->get_option_text().empty()) return {};
		return " " + opt->get_option_text();
	}

	std::string make_option_desc(const CLI::Option *opt) const override {
		std::string out;

		if (!opt->get_description().empty()) out += opt->get_description();

		if (!opt->get_envname().empty()) {
			if (!out.empty()) out += " ";
			out += "(Env: " + opt->get_envname();
		} else if (!out.empty()) {
			out += " (";
		} else {
			out += "(";
		}

		if (!opt->get_default_str().empty()) {
			if (out.back() != '(') out += ", ";
			out += "default: " + opt->get_default_str();
		}

		if (out.back() == '(') out.pop_back();
		else out += ")";

		return out;
	}
};

/// The values --appendfsync accepts: the enumerators' names.
std::vector<std::string> fsync_policy_names() {
	std::vector<std::string> names;
	for (const std::string_view name : magic_enum::enum_names<FsyncPolicy>())
		names.emplace_back(name);
	return names;
}
} // namespace

CLI::App_p setup_cli_options() {
	auto app = std::make_shared<CLI::App>("Redis-like server");
	app->formatter(std::make_shared<MyFormatter>());
	// Version + config
	app->set_version_flag("--version",
						  fmt::format("version={}, bits={}",
									  "1.0.0",
									  sizeof(long) == 4 ? 32 : 64));
	app->set_config("--config", "", "Config file path")
		->option_text("<file>")
		->check(CLI::ExistingFile)
		->envname("REDIS_CONFIG_FILE");

	app->add_option("--host", "Address to listen on")
		->envname("REDIS_HOST")
		->option_text("<hostname>")
		->group("Network")
		->check(CLI::ValidIPV4)
		->default_val("127.0.0.1");
	app->add_option("--io-threads",
					"Network I/O threads. 1 serves everything on one thread; "
					"more adds a thread that executes commands, so I/O and "
					"parsing run in parallel")
		->envname("REDIS_IO_THREADS")
		->option_text("<count>")
		->group("Network")
		->check(CLI::Range(1, 256))
		->default_val(1);
	app->add_flag("--sender-sessions",
				  "Serve clients with sender-based sessions (stdexec) "
				  "instead of coroutines")
		->envname("REDIS_SENDER_SESSIONS")
		->group("Network")
		->default_val(false);
	app->add_option("--dump","Snapshot file, loaded at startup and written "
							 "by SAVE, BGSAVE and on shutdown")
		->envname("REDIS_DUMP_FILE")
		->option_text("<file>")
		->group("Persistence")
		->default_val("dump.snap");
	app->add_flag("--appendonly",
				  "Log every write command to an append-only file and replay "
				  "it at startup, so writes since the last snapshot survive a "
				  "crash")
		->envname("REDIS_APPENDONLY")
		->group("Persistence")
		->default_val(false);
	app->add_option("--appendfsync",
					"When the append-only file is forced to disk: 'always' "
					"before replying, 'everysec' once a second, 'no' never")
		->envname("REDIS_APPENDFSYNC")
		->option_text("<policy>")
		->group("Persistence")
		->check(CLI::IsMember(fsync_policy_names()))
		->default_val(magic_enum::enum_name(AofConfig{}.fsync));
	app->add_option("--appenddir", "Directory of the append-only files")
		->envname("REDIS_APPENDDIR")
		->option_text("<dir>")
		->group("Persistence")
		->default_val("appendonlydir");
	app->add_option("-p,--port", "Port to listen on")
		->envname("REDIS_PORT")
		->option_text("<port>")
		->group("Network")
		->check(CLI::PositiveNumber)
		->default_val(6379);
	
	app->add_option("--log-file", "Log file path")
		->envname("REDIS_LOGFILE")
		->group("Logging")
		->default_val("redis_server.log")
		->option_text("<file>");
	app->add_option("--log-level", "Log level")
		->envname("REDIS_LOGLEVEL")
		->group("Logging")
		->default_val("info");
	return app;
}

} // namespace redis
