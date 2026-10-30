#include "../commands/commands.fixture.hpp"
#include "server/net/pipeline.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace redis;

SCENARIO("Splitting the hot path into stages", "[net][pipeline]") {
	GIVEN("a server and an empty reply buffer") {
		test::Server server;
		const CommandHandler handler;
		std::string replies;
		ParsedBatch batch;

		WHEN("a buffer with pipelined and half a request is parsed") {
			std::string pending = "PING\r\n*2\r\n$3\r\nGET\r\n$1\r\nk\r\n"
								  "*2\r\n$3\r\nGET\r\n$1";
			const std::string received = pending;
			parse_pending(pending, batch);

			THEN("complete requests are cut, but their bytes stay for now") {
				REQUIRE(batch.size() == 2);
				CHECK(batch.request(0).size() == 1);
				CHECK(batch.request(1).size() == 2);
				CHECK(batch.protocol_error.empty());
				CHECK(pending == received);
				CHECK(replies.empty());
			}

			AND_WHEN("the batch is executed and its bytes dropped") {
				const auto after =
					execute_batch(batch, replies, handler, server.ctx);
				drop_consumed(pending, batch);

				THEN("replies come back in request order") {
					CHECK(after == AfterReplies::KeepOpen);
					CHECK(replies == "+PONG\r\n$-1\r\n");
				}
				THEN("only the partial request is left") {
					CHECK(pending == "*2\r\n$3\r\nGET\r\n$1");
					CHECK(batch.size() == 0);
				}
			}
		}

		WHEN("a malformed request follows a valid one") {
			std::string pending = "PING\r\n*1\r\n$3\r\nabcXY";
			parse_pending(pending, batch);

			THEN("the valid request is kept and the error is recorded") {
				CHECK(batch.size() == 1);
				CHECK(batch.protocol_error.starts_with("-ERR Protocol error"));
			}

			AND_WHEN("the batch is executed") {
				const auto after =
					execute_batch(batch, replies, handler, server.ctx);

				THEN("the error reply follows the valid one, then close") {
					CHECK(after == AfterReplies::Close);
					CHECK(replies.starts_with("+PONG\r\n-ERR Protocol error"));
				}
			}
		}
	}
}

SCENARIO("Executing buffered requests", "[net][pipeline]") {
	GIVEN("a server and an empty reply buffer") {
		test::Server server;
		const CommandHandler handler;
		std::string replies;
		ParsedBatch batch;

		WHEN("the buffer holds several pipelined requests") {
			std::string pending =
				"*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$1\r\nv\r\n"
				"*2\r\n$3\r\nGET\r\n$1\r\nk\r\n"
				"PING\r\n";
			const auto after =
				execute_pending(pending, batch, replies, handler, server.ctx);

			THEN("every reply is appended in order and the buffer is drained") {
				CHECK(after == AfterReplies::KeepOpen);
				CHECK(replies == "+OK\r\n$1\r\nv\r\n+PONG\r\n");
				CHECK(pending.empty());
			}
		}

		WHEN("the buffer ends with half a request") {
			std::string pending = "PING\r\n*2\r\n$3\r\nGET\r\n$1";
			const auto after =
				execute_pending(pending, batch, replies, handler, server.ctx);

			THEN("complete requests run and the partial one is kept") {
				CHECK(after == AfterReplies::KeepOpen);
				CHECK(replies == "+PONG\r\n");
				CHECK(pending == "*2\r\n$3\r\nGET\r\n$1");
			}

			AND_WHEN("the rest of the request arrives") {
				pending += "\r\nk\r\n";
				replies.clear();
				const auto rest =
					execute_pending(pending, batch, replies, handler, server.ctx);

				THEN("it runs as one request") {
					CHECK(rest == AfterReplies::KeepOpen);
					CHECK(replies == "$-1\r\n");
					CHECK(pending.empty());
				}
			}
		}

		WHEN("a malformed request follows a valid one") {
			// Declares 3 bytes but "abc" is followed by "XY", not CRLF.
			std::string pending = "PING\r\n*1\r\n$3\r\nabcXY";
			const auto after =
				execute_pending(pending, batch, replies, handler, server.ctx);

			THEN("the valid one is answered, then the error, then close") {
				CHECK(after == AfterReplies::Close);
				CHECK(replies.starts_with("+PONG\r\n-ERR Protocol error"));
				CHECK(replies.ends_with("\r\n"));
			}
		}

		WHEN("a well-formed RESP value that is not a command comes first") {
			std::string pending = "*0\r\nPING\r\n";
			const auto after    = execute_pending(pending, batch, replies,
												  handler, server.ctx);

			THEN("it gets an error reply and the connection stays open") {
				CHECK(after == AfterReplies::KeepOpen);
				CHECK(replies.starts_with("-ERR Protocol error"));
				CHECK(replies.ends_with("\r\n+PONG\r\n"));
				CHECK(pending.empty());
			}
		}

		WHEN("the buffer holds only blank inline lines") {
			std::string pending = "\r\n\r\n";
			const auto after =
				execute_pending(pending, batch, replies, handler, server.ctx);

			THEN("they are consumed without a reply") {
				CHECK(after == AfterReplies::KeepOpen);
				CHECK(replies.empty());
				CHECK(pending.empty());
			}
		}
	}
}
