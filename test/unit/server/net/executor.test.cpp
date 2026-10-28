#include "server/net/v2/asio_context.hpp"
#include "server/net/executor.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <catch2/catch_test_macros.hpp>
#include <exec/start_detached.hpp>

#include <stdexcept>
#include <string>
#include <thread>

using namespace redis;

namespace {
/// Run \p body on a fresh io_context to completion, on this thread.
template <typename Body>
void run_coroutine(Body body) {
	net::io_context io;
	net::co_spawn(io, std::move(body), net::detached);
	io.run();
}
} // namespace

SCENARIO("Running work on the keyspace thread", "[net][executor]") {
	GIVEN("an executor with a dedicated thread") {
		Executor executor(true);
		const auto caller = std::this_thread::get_id();

		WHEN("a coroutine awaits a function") {
			std::thread::id ran_on;
			std::thread::id resumed_on;
			int result = 0;
			run_coroutine([&]() -> net::awaitable<void> {
				result = co_await executor.run([&] {
					ran_on = std::this_thread::get_id();
					return 42;
				});
				resumed_on = std::this_thread::get_id();
			});

			THEN("it runs off the I/O thread and resumes back on it") {
				CHECK(executor.is_threaded());
				CHECK(result == 42);
				CHECK(ran_on != caller);
				CHECK(resumed_on == caller);
			}
		}

		WHEN("the function returns nothing") {
			bool ran = false;
			run_coroutine([&]() -> net::awaitable<void> {
				co_await executor.run([&] { ran = true; });
			});

			THEN("the coroutine still resumes after it ran") {
				CHECK(ran);
			}
		}

		WHEN("the function throws") {
			std::string caught;
			run_coroutine([&]() -> net::awaitable<void> {
				try {
					co_await executor.run(
						[]() -> int { throw std::runtime_error("boom"); });
				} catch (const std::runtime_error &e) {
					caught = e.what();
				}
			});

			THEN("the exception surfaces from the co_await") {
				CHECK(caught == "boom");
			}
		}

		WHEN("many coroutines each queue work") {
			// Deliberately not atomic: only the executor thread touches it.
			int counter              = 0;
			constexpr int coroutines = 32;
			constexpr int per_task   = 100;
			net::io_context io;
			for (int i = 0; i < coroutines; ++i)
				net::co_spawn(
					io,
					[&]() -> net::awaitable<void> {
						for (int n = 0; n < per_task; ++n)
							co_await executor.run([&] { ++counter; });
					},
					net::detached);
			io.run();
			executor.stop();

			THEN("every call ran, one at a time") {
				CHECK(counter == coroutines * per_task);
			}
		}
	}

	GIVEN("an executor without a dedicated thread") {
		Executor executor(false);

		WHEN("a coroutine awaits a function") {
			std::thread::id ran_on;
			run_coroutine([&]() -> net::awaitable<void> {
				co_await executor.run(
					[&] { ran_on = std::this_thread::get_id(); });
			});

			THEN("it runs inline on the calling thread") {
				CHECK(!executor.is_threaded());
				CHECK(ran_on == std::this_thread::get_id());
			}
		}
	}
}

SCENARIO("Running work on the keyspace thread as a sender", "[net][executor]") {
	const auto caller = std::this_thread::get_id();

	GIVEN("an io_context and its executor as a scheduler") {
		net::io_context io;
		auto guard = net::make_work_guard(io);
		AsioContext back(io.get_executor());

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
							  std::rethrow_exception(e);
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
