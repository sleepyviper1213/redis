#include "common/resp/value.hpp"
#include "core/chrono/elapsed_timer.hpp"
#include "server/net/detail/asio_context.hpp"
#include "server/net/executor.hpp"
#include "server/services.hpp"
#include "support/temp_file.hpp"

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <catch2/catch_test_macros.hpp>
#include <exec/start_detached.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace redis;
using namespace std::chrono_literals;
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

namespace {

/// Long enough that the batch is still running when the test destroys what it
/// runs on, and the ones queued behind it have not started.
constexpr auto BATCH_TIME = 30ms;

/// Work for the keyspace thread that says when it began, takes its time, then
/// records its place. Only the keyspace thread writes \p order.
struct SlowBatch {
	std::atomic<bool> *started;
	std::vector<int> *order;
	int index;

	void operator()() const {
		started->store(true);
		std::this_thread::sleep_for(BATCH_TIME);
		order->push_back(index);
	}
};

/// A command that reaches the keyspace only after a pause, so the destructor
/// that waits for it has begun by then.
struct SlowSet {
	Services *services;
	std::atomic<bool> *started;
	std::string *reply;

	void operator()() const {
		started->store(true);
		std::this_thread::sleep_for(BATCH_TIME);
		const std::array<std::string, 3> argv{"SET", "k", "v"};
		*reply = services->handler.execute(services->ctx,
										   resp::Value::from_raw_array(argv));
	}
};

/// True once \p flag is set, false if \p limit passes first.
bool set_within(const std::atomic<bool> &flag,
				std::chrono::milliseconds limit) {
	const ElapsedTimer timer;
	while (!flag.load()) {
		if (timer.has_expired(limit)) return false;
		std::this_thread::yield();
	}
	return true;
}

/// How long to wait for the work to complete: a destructor that dropped it
/// would otherwise leave the loop waiting for ever, and a CI job hung.
constexpr auto DRAIN_LIMIT = 10s;

/// Runs \p io until everything in \p scope has completed: the continuations of
/// work that finished on the keyspace thread are queued on it. False if it did
/// not within DRAIN_LIMIT.
template <class Guard>
[[nodiscard]] bool drain(net::io_context &io, Guard &guard,
						 detail::AsioContext &back,
						 stdexec::simple_counting_scope &scope) {
	exec::start_detached(stdexec::starts_on(back.get_scheduler(), scope.join()) |
						 stdexec::then([&guard] { guard.reset(); }));
	io.run_for(DRAIN_LIMIT);
	return io.stopped();
}

} // namespace

SCENARIO("Destroying the executor waits for the work queued on it",
		 "[net][executor][shutdown]") {
	constexpr int BATCHES = 5;

	GIVEN("a threaded executor with a batch running and more queued") {
		net::io_context io;
		auto guard = net::make_work_guard(io);
		detail::AsioContext back(io.get_executor());
		auto executor = std::make_unique<Executor>(true);
		std::atomic<bool> started = false;
		std::vector<int> order;
		stdexec::simple_counting_scope scope;
		for (int i = 0; i < BATCHES; ++i)
			stdexec::spawn(
				executor->run_on(SlowBatch{&started, &order, i},
								 back.get_scheduler()) |
					stdexec::upon_error(
						[](const std::exception_ptr &) noexcept {}),
				scope.get_token());
		REQUIRE(set_within(started, 5s));

		WHEN("TC-296: it is destroyed") {
			executor.reset();
			const bool drained = drain(io, guard, back, scope);

			THEN("every batch ran, in the order queued, before it returned") {
				std::vector<int> queued(BATCHES);
				for (int i = 0; i < BATCHES; ++i) queued[i] = i;
				CHECK(drained);
				CHECK(order == queued);
			}
		}
	}
}

SCENARIO("Destroying the services waits for the keyspace thread first",
		 "[net][executor][shutdown]") {
	GIVEN("threaded services with a command on its way to the keyspace") {
		net::io_context io;
		auto guard = net::make_work_guard(io);
		detail::AsioContext back(io.get_executor());
		const test::TempFile dump;
		auto services = std::make_unique<Services>(dump.path(), true);
		std::atomic<bool> started = false;
		std::string reply;
		stdexec::simple_counting_scope scope;
		stdexec::spawn(
			services->keyspace.run_on(SlowSet{services.get(), &started, &reply},
									  back.get_scheduler()) |
				stdexec::upon_error([](const std::exception_ptr &) noexcept {}),
			scope.get_token());
		REQUIRE(set_within(started, 5s));

		WHEN("TC-296: the services are destroyed") {
			services.reset();
			const bool drained = drain(io, guard, back, scope);

			THEN("the command ran to the end against a live keyspace") {
				CHECK(drained);
				CHECK(reply == "+OK\r\n");
			}
		}
	}
}
