#include "../commands/commands.fixture.hpp"
#include "server/net/guard.hpp"
#include "server/net/detail/pipeline.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <latch>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace redis;

SCENARIO("The guard admits clients up to its limit", "[net][guard]") {
	GIVEN("a guard that allows two clients") {
		NetGuard guard(NetLimits{.max_clients = 2});

		WHEN("two clients are admitted") {
			auto admitted_first  = guard.admit();
			auto admitted_second = guard.admit();
			REQUIRE(admitted_first);
			REQUIRE(admitted_second);
			std::optional<NetGuard::Ticket> first(std::move(*admitted_first));
			std::optional<NetGuard::Ticket> const second(std::move(*admitted_second));
			CHECK(guard.clients() == 2);

			THEN("a third is refused and does not take a slot") {
				const auto third = guard.admit();
				REQUIRE_FALSE(third);
				CHECK(third.error() == Violation::TooManyClients);
				CHECK(guard.clients() == 2);
			}
			AND_WHEN("one leaves") {
				first.reset();
				THEN("its slot can be taken again") {
					CHECK(guard.clients() == 1);
					CHECK(guard.admit());
				}
			}
		}

		WHEN("a ticket is moved") {
			std::optional<NetGuard::Ticket> kept;
			{
				auto ticket = guard.admit();
				REQUIRE(ticket);
				kept.emplace(std::move(*ticket));
			}
			THEN("the slot is held until the last owner is gone") {
				CHECK(guard.clients() == 1);
				kept.reset();
				CHECK(guard.clients() == 0);
			}
		}
	}
}

SCENARIO("The guard never admits more clients than its limit under contention",
		 "[net][guard][concurrency]") {
	constexpr size_t LIMIT   = 4;
	constexpr size_t THREADS = 16;
	constexpr size_t ATTEMPTS_PER_THREAD = 100;
	constexpr size_t CHURN_ROUNDS        = 5000;

	GIVEN("a guard that allows four clients and sixteen racing threads") {
		NetGuard guard(NetLimits{.max_clients = LIMIT});

		WHEN("every thread tries to take a slot and keeps it") {
			std::latch start(THREADS);
			std::atomic<size_t> granted{0};
			std::vector<std::vector<NetGuard::Ticket>> held(THREADS);
			{
				std::vector<std::jthread> threads;
				for (size_t t = 0; t < THREADS; ++t)
					threads.emplace_back([&, t] {
						start.arrive_and_wait();
						for (size_t i = 0; i < ATTEMPTS_PER_THREAD; ++i)
							if (auto ticket = guard.admit()) {
								held[t].push_back(std::move(*ticket));
								granted.fetch_add(1);
							}
					});
			}
			THEN("exactly the limit is admitted, however the race falls") {
				CHECK(granted.load() == LIMIT);
				CHECK(guard.clients() == LIMIT);
				CHECK(guard.admitted() == LIMIT);
			}
			AND_WHEN("all the tickets are released") {
				held.clear();
				THEN("no slot is left behind") {
					CHECK(guard.clients() == 0);
				}
			}
		}

		WHEN("every thread takes and releases slots over and over") {
			std::latch start(THREADS);
			std::atomic<size_t> holders{0};
			std::atomic<size_t> peak{0};
			{
				std::vector<std::jthread> threads;
				for (size_t t = 0; t < THREADS; ++t)
					threads.emplace_back([&] {
						start.arrive_and_wait();
						for (size_t i = 0; i < CHURN_ROUNDS; ++i) {
							auto ticket = guard.admit();
							if (!ticket) continue;
							const size_t now = holders.fetch_add(1) + 1;
							size_t seen      = peak.load();
							while (now > seen &&
								   !peak.compare_exchange_weak(seen, now)) {}
							holders.fetch_sub(1);
						}
					});
			}
			THEN("the holders at one moment never pass the limit") {
				CHECK(peak.load() <= LIMIT);
				CHECK(peak.load() >= 1);
			}
			THEN("every slot comes back") {
				CHECK(guard.clients() == 0);
				CHECK(holders.load() == 0);
			}
		}
	}
}

SCENARIO("The guard bounds the buffers", "[net][guard]") {
	NetGuard guard(NetLimits{.max_query_buffer = 100, .max_output_buffer = 50});

	THEN("input at the limit is fine, one byte over is not") {
		CHECK(guard.check_input(100));
		const auto over = guard.check_input(101);
		REQUIRE_FALSE(over);
		CHECK(over.error() == Violation::QueryBufferFull);
	}
	THEN("output at the limit is fine, one byte over is not") {
		CHECK(guard.check_output(50));
		const auto over = guard.check_output(51);
		REQUIRE_FALSE(over);
		CHECK(over.error() == Violation::OutputBufferFull);
	}
	THEN("recorded violations are counted by kind") {
		guard.record(Violation::QueryBufferFull, "peer", 101);
		guard.record(Violation::QueryBufferFull, "peer", 102);
		guard.record(Violation::IdleTimeout, "peer", 5);
		CHECK(guard.count(Violation::QueryBufferFull) == 2);
		CHECK(guard.count(Violation::IdleTimeout) == 1);
		CHECK(guard.count(Violation::TooManyClients) == 0);
	}
	THEN("every violation has a description") {
		for (const auto violation :
			 {Violation::TooManyClients, Violation::QueryBufferFull,
			  Violation::OutputBufferFull, Violation::IdleTimeout,
			  Violation::RequestTimeout})
			CHECK_FALSE(format_as(violation).empty());
	}
}

SCENARIO("A batch stops once its replies pass the output limit",
		 "[net][guard][pipeline]") {
	GIVEN("a server holding a 100-byte value and a pipelined GET x 10") {
		test::Server server;
		const CommandHandler handler;
		server.run({"SET", "k", std::string(100, 'x')});

		std::string pending;
		for (int i = 0; i < 10; ++i) pending += "GET k\r\n";
		std::string replies;
		detail::ParsedBatch batch;
		detail::parse_pending(pending, batch);
		REQUIRE(batch.size() == 10);

		WHEN("the limit is 200 bytes") {
			// A reply is "$100\r\n" + 100 bytes + "\r\n" = 108 bytes.
			const auto after =
				detail::execute_batch(batch, replies, handler, server.ctx, 200);

			THEN("execution stops after the reply that crossed it") {
				CHECK(after == detail::AfterReplies::Close);
				// 108 bytes is under 200, 216 is over: the second reply
				// crosses the limit and the other eight never run.
				CHECK(replies.size() == 2 * 108);
			}
		}
		WHEN("the limit is not reached") {
			const auto after =
				detail::execute_batch(batch, replies, handler, server.ctx, 1'000'000);
			THEN("every request runs") {
				CHECK(after == detail::AfterReplies::KeepOpen);
				CHECK(replies.size() == 10 * 108);
			}
		}
	}
}
