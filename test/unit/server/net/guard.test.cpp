#include "../commands/commands.fixture.hpp"
#include "server/net/guard.hpp"
#include "server/net/pipeline.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <utility>

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
			std::optional<NetGuard::Ticket> second(std::move(*admitted_second));
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
			  Violation::OutputBufferFull, Violation::IdleTimeout})
			CHECK_FALSE(to_string(violation).empty());
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
		ParsedBatch batch;
		parse_pending(pending, batch);
		REQUIRE(batch.size() == 10);

		WHEN("the limit is 200 bytes") {
			// A reply is "$100\r\n" + 100 bytes + "\r\n" = 108 bytes.
			const auto after =
				execute_batch(batch, replies, handler, server.ctx, 200);

			THEN("execution stops after the reply that crossed it") {
				CHECK(after == AfterReplies::Close);
				// 108 bytes is under 200, 216 is over: the second reply
				// crosses the limit and the other eight never run.
				CHECK(replies.size() == 2 * 108);
			}
		}
		WHEN("the limit is not reached") {
			const auto after =
				execute_batch(batch, replies, handler, server.ctx, 1'000'000);
			THEN("every request runs") {
				CHECK(after == AfterReplies::KeepOpen);
				CHECK(replies.size() == 10 * 108);
			}
		}
	}
}
