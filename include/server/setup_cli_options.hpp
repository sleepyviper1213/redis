#pragma once

#include <CLI/CLI.hpp>

namespace redis {
CLI::App_p setup_cli_options_from(int argc, char **argv);
} // namespace redis
