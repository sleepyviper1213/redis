#include "net/co_main.hpp"
#include "config.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <spdlog/spdlog.h>

int main(int argc, char **argv) {
	try {
		auto cfg = redis::read(argc, argv);

		boost::asio::io_context ioc;
		boost::asio::co_spawn(ioc, co_main(cfg), [](std::exception_ptr p) {
			if (p) std::rethrow_exception(p);
		});
		ioc.run();

	} catch (const std::exception &e) {
		spdlog::error("(main) {}", e.what());
		return 1;
	}
}
