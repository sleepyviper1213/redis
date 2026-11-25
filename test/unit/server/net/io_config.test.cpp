#include "server/net/detail/io_config.hpp"

#include <boost/asio/config.hpp>
#include <boost/asio/io_context.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace redis;
namespace asio = boost::asio;

SCENARIO("The io_context is configured as it was, plus the reactor setting",
		 "[net][config]") {
	GIVEN("a number of I/O threads") {
		const unsigned threads = GENERATE(1U, 2U, 4U);

		WHEN("TC-299: an io_context is built from io_config() and another from "
			 "the same number as a concurrency hint") {
			asio::io_context configured(detail::io_config(threads));
			asio::io_context hinted(static_cast<int>(threads));
			const asio::config now(configured);
			const asio::config before(hinted);

			THEN("the scheduler's hint and locking are the same") {
				CHECK(now.get("scheduler", "concurrency_hint", -7) ==
					  before.get("scheduler", "concurrency_hint", -7));
				CHECK(now.get("scheduler", "locking", true) ==
					  before.get("scheduler", "locking", true));
			}

			AND_THEN("so is the reactor's locking") {
				CHECK(now.get("reactor", "io_locking", true) ==
					  before.get("reactor", "io_locking", true));
				CHECK(now.get("reactor", "registration_locking", true) ==
					  before.get("reactor", "registration_locking", true));
			}

			AND_THEN("a partial read resets the edge where Asio uses epoll") {
#ifdef BOOST_ASIO_HAS_EPOLL
				CHECK(now.get("reactor", "reset_edge_on_partial_read", 0) == 1);
#else
				CHECK(now.get("reactor", "reset_edge_on_partial_read", 0) == 0);
#endif
			}
		}
	}
}

SCENARIO("No I/O threads count as one", "[net][config]") {
	GIVEN("io_config(0)") {
		asio::io_context configured(detail::io_config(0));
		const asio::config now(configured);

		THEN("the scheduler's hint is 1") {
			CHECK(now.get("scheduler", "concurrency_hint", -7) == 1);
		}
	}
}
