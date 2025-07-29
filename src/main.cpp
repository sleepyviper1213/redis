#include "net.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <fmt/base.h>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>

#include <chrono>

/// @brief Port on which the HTTP server listens.
constexpr int PORT = 8080;

/// @brief Maximum number of server threads (not used directly here, reserved
/// for future use).
constexpr int MAX_THREADS = 4;

/// @brief Connection timeout for inactive clients.
constexpr auto CONNECTION_TIMEOUT = std::chrono::seconds(180);

int main() {
	spdlog::set_level(spdlog::level::debug); // Set *global* log level to debug

	try {
		net::io_context io_ctx;

		boost::asio::co_spawn(io_ctx,
							  redis::listener({redis::tcp::v4(), PORT}),
							  boost::asio::detached);

		spdlog::info("[MAIN] Web server started. Listening on port 8080.");
		io_ctx.run();
	} catch (const boost::system::error_code &e) {
		spdlog::error("Fatal error: {}", e.what());
	}
}
