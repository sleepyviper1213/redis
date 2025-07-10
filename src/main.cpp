#include "gate.hpp"
#include "http_server.hpp"

#include <spdlog/spdlog.h>

constexpr int PORT                = 8080;
constexpr int MAX_THREADS         = 4;
constexpr auto CONNECTION_TIMEOUT = std::chrono::seconds(180);

int main() {
	try {
		net::io_context io_ctx(1);

		Gate db;
		auto dqr = std::make_shared<dbQueryResource>(&db);

		boost::asio::co_spawn(io_ctx,
							  listener({tcp::v4(), 8080}, dqr),
							  boost::asio::detached);

		spdlog::info("[MAIN] Web server started. Listening on port 8080.");
		io_ctx.run();
	} catch (const std::exception &e) {
		fmt::println(stderr, "Fatal error: {}", e.what());
	}
}
