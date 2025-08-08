#include "net.hpp"
#include "net/tcp_server.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <fmt/base.h>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>

#include <chrono>

/// @brief Port on which the HTTP server listens.
constexpr int PORT = 6379;


/// @brief Connection timeout for inactive clients.
constexpr auto CONNECTION_TIMEOUT = std::chrono::seconds(180);

int main() {
	spdlog::set_level(spdlog::level::debug); // Set *global* log level to debug

	redis::Server server(PORT);
	server.start();

	spdlog::info("[MAIN] Web server started. Listening on port 8080.");
}
