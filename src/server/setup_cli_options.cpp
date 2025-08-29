#include "../../include/server/setup_cli_options.hpp"

#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace redis {
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

		if (opt->get_default_str().size() > 0) {
			if (out.back() != '(') out += ", ";
			out += "default: " + opt->get_default_str();
		}

		if (out.back() == '(') out.pop_back();
		else out += ")";

		return out;
	}
};

CLI::App_p setup_cli_options_from(int argc, char **argv) {
	auto app = std::make_shared<CLI::App>("Redis-like TcpServer");
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

	app->add_option("--host", "TcpServer host")
		->envname("REDIS_HOST")
		->option_text("<hostname>")
		->group("Network")
		->check(CLI::ValidIPV4)
		->default_val("127.0.0.1");
	app->add_option("--dump")->option_text("<file>")->default_val("dump.rdb");
	app->add_option("-p,--port", "TcpServer port")
		->envname("REDIS_PORT")
		->option_text("<port>")
		->group("Network")
		->check(CLI::PositiveNumber)
		->default_val(6379);
	app->add_flag("--use-ssl", "Enable SSL")
		->envname("REDIS_USE_SSL")
		->group("Network");

	app->add_option("--logfile", "Log file path")
		->envname("REDIS_LOGFILE")
		->group("Logging")
		->default_val(6379)
		->check(CLI::ExistingFile)
		->option_text("<file>");
	app->add_option("--loglevel", "Log level")
		->envname("REDIS_LOGLEVEL")
		->group("Logging")
		->default_val("info");
	return app;
}

} // namespace redis
