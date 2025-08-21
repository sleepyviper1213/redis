#pragma once

#include <CLI/CLI.hpp>

namespace redis {
CLI::App_p configure_from_cli(int argc, char **argv);
} // namespace redis
