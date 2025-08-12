#include "net.hpp"

#include <spdlog/spdlog.h>

#include <chrono>

/// @brief Connection timeout for inactive clients.
constexpr auto CONNECTION_TIMEOUT = std::chrono::seconds(180);

int main() {
	spdlog::set_level(spdlog::level::debug); // Set *global* log level to debug
	/// @brief Port on which the HTTP server listens.
	constexpr int PORT = 6379;
	redis::Server server(PORT);
	server.start();
}
