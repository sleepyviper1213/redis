#pragma once
#include "utils/time.hpp"

#include <CLI/CLI.hpp>

#include <chrono>

namespace redis {

/**
 * \brief Address of a Redis server
 */
struct address {
	std::string_view host = "127.0.0.1";
	int port              = 6379;
};

/**
 * @brief Configure parameters used by the connection classes
 * @ingroup high-level-api
 */
struct Config {
	/// Uses SSL instead of a plain connection.
	bool use_ssl = false;

	/// Address of the Redis server.
	address addr;
};

redis::Config configure_from_cli(int argc, char **argv);
} // namespace redis
