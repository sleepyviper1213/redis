#include "actions/gate.hpp"
#include "net.hpp"

#include <spdlog/spdlog.h>

#include <memory>


constexpr int PORT                = 8080;
constexpr int MAX_THREADS         = 4;
constexpr auto CONNECTION_TIMEOUT = std::chrono::seconds(180);

int main() {
	try {
		net::io_context io_ctx(1);

		auto db = std::make_unique<Gate>();
		dbQueryResource dqr(db.get());

		boost::asio::co_spawn(io_ctx,
							  listener({tcp::v4(), PORT}, dqr),
							  boost::asio::detached);

		spdlog::info("[MAIN] Web server started. Listening on port 8080.");
		io_ctx.run();
	} catch (const std::exception &e) {
		fmt::println(stderr, "Fatal error: {}", e.what());
	}
}
