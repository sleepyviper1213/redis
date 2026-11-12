#include "server/net/detail/asio_context.hpp"
#include "server/net/executor.hpp"

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <catch2/catch_test_macros.hpp>
#include <exec/start_detached.hpp>

#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

using namespace redis;
namespace net = boost::asio;

SCENARIO("Running work on the keyspace thread", "[net][executor]") {
	const auto caller = std::this_thread::get_id();

	GIVEN("an io_context and its executor as a scheduler") {
		net::io_context io;
		auto guard = net::make_work_guard(io);
		detail::AsioContext back(io.get_executor());

		AND_GIVEN("an executor with a dedicated thread") {
			Executor executor(true);

			WHEN("a sender runs a function and continues on the I/O thread") {
				std::thread::id ran_on;
				std::thread::id resumed_on;
				int result = 0;
				exec::start_detached(
					executor.run_on(
						[&] {
							ran_on = std::this_thread::get_id();
							return 42;
						},
						back.get_scheduler())
					| stdexec::then([&](int value) {
						  result     = value;
						  resumed_on = std::this_thread::get_id();
						  guard.reset();
					  }));
				io.run();

				THEN("it ran off the I/O thread and continued back on it") {
					CHECK(result == 42);
					CHECK(ran_on != caller);
					CHECK(resumed_on == caller);
				}
			}

			WHEN("the function throws") {
				std::string caught;
				exec::start_detached(
					executor.run_on(
						[]() -> int { throw std::runtime_error("boom"); },
						back.get_scheduler())
					| stdexec::then([&](int) { guard.reset(); })
					| stdexec::upon_error([&](std::exception_ptr e) {
						  try {
							  std::rethrow_exception(std::move(e));
						  } catch (const std::runtime_error &ex) {
							  caught = ex.what();
						  }
						  guard.reset();
					  }));
				io.run();

				THEN("the sender completes with that error") {
					CHECK(caught == "boom");
				}
			}
			WHEN("many senders each queue work") {
				// Deliberately not atomic: only the executor thread touches it.
				int counter           = 0;
				constexpr int senders = 32 * 100;
				stdexec::simple_counting_scope scope;
				for (int i = 0; i < senders; ++i)
					stdexec::spawn(
						executor.run_on([&] { ++counter; },
										back.get_scheduler()) |
							stdexec::upon_error(
								[](const std::exception_ptr &) noexcept {}),
						scope.get_token());
				exec::start_detached(
					stdexec::starts_on(back.get_scheduler(), scope.join()) |
					stdexec::then([&] { guard.reset(); }));
				io.run();
				executor.stop();

				THEN("every call ran, one at a time") {
					CHECK(counter == senders);
				}
			}
		}

		AND_GIVEN("an executor without a dedicated thread") {
			Executor executor(false);

			WHEN("a sender runs a function") {
				std::thread::id ran_on;
				exec::start_detached(
					executor.run_on(
						[&] { ran_on = std::this_thread::get_id(); },
						back.get_scheduler())
					| stdexec::then([&] { guard.reset(); }));
				io.run();

				THEN("it runs inline where the sender started") {
					CHECK(ran_on == caller);
				}
			}
		}
	}
}
