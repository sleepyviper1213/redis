// TC-278 to TC-290 (ADR-0012): what a session decides, from strings, with no
// socket, scheduler or clock. Each behaviour is asserted in both modes: inline
// (--io-threads 1) and threaded (parse on the session, execute on the keyspace
// thread; here execute_ingested_requests() is simply called on the test's thread).
#include "../commands/commands.fixture.hpp"
#include "server/net/detail/conversation.hpp"
#include "server/net/guard.hpp"
#include "server/net/net_error.hpp"

#include <boost/asio/error.hpp>
#include <boost/system/system_error.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

using namespace redis;
using namespace std::chrono_literals;
using detail::Conversation;

namespace {

/// A conversation over a real guard, handler and keyspace, driven by strings.
class Rig {
public:
	Rig(bool threaded, NetLimits limits = {})
		: guard(limits),
		  conversation(Conversation::Parts{.guard    = guard,
										   .handler  = handler,
										   .ctx      = server.ctx,
										   .is_threaded = threaded},
					   "peer:1") {}

	/// Bytes arrive: whether there is a batch to run.
	bool feed(std::string_view bytes) {
		const auto room = conversation.read_buffer();
		REQUIRE(bytes.size() <= room.size());
		std::ranges::copy(bytes, room.begin());
		return conversation.ingest_read_bytes(bytes.size());
	}

	/// Bytes arrive and, if that makes a batch, it runs. What it answered,
	/// taken out of the conversation as the session would send it.
	std::string answer(std::string_view bytes) {
		if (feed(bytes)) conversation.execute_ingested_requests();
		std::string replies(conversation.unsent_replies());
		conversation.discard_sent_replies();
		return replies;
	}

	test::Server server;
	CommandHandler handler;
	NetGuard guard;
	Conversation conversation;
};

/// The reply to GET of a 100-byte value: "$100", CRLF, the value, CRLF.
constexpr size_t VALUE_SIZE     = 100;
constexpr size_t REPLY_SIZE     = 6 + VALUE_SIZE + 2;
constexpr size_t QUERY_LIMIT    = 64;
constexpr size_t OUTPUT_LIMIT   = 200;
constexpr size_t PIPELINED_GETS = 10;

constexpr std::string_view PING_RESP = "*1\r\n$4\r\nPING\r\n";

} // namespace

SCENARIO("A conversation takes in what a client sends", "[net][conversation]") {
	const bool threaded = GENERATE(false, true);
	CAPTURE(threaded);
	Rig rig(threaded);

	GIVEN("a request cut in two reads") {
		const std::string_view first  = "*1\r\n$4\r\nPI";
		const std::string_view second = "NG\r\n";

		WHEN("a read brings nothing (TC-278)") {
			CHECK(rig.answer(first).empty());
			CHECK_FALSE(rig.conversation.ingest_read_bytes(0));

			THEN("the half request is kept, and nothing was counted") {
				CHECK(rig.conversation.unsent_replies().empty());
				CHECK_FALSE(rig.conversation.is_over());
				CHECK(rig.answer(second) == "+PONG\r\n");
				CHECK(rig.guard.count(Violation::QueryBufferFull) == 0);
			}
		}

		WHEN("the first half is read (TC-280)") {
			const std::string early = rig.answer(first);

			THEN("it gets no reply, and the rest completes it") {
				CHECK(early.empty());
				CHECK_FALSE(rig.conversation.is_over());
				CHECK(rig.answer(second) == "+PONG\r\n");
			}
		}
	}

	GIVEN("a complete request (TC-279)") {
		const std::string reply = rig.answer("PING\r\n");

		THEN("it is answered and the conversation goes on") {
			CHECK(reply == "+PONG\r\n");
			CHECK_FALSE(rig.conversation.is_over());
			CHECK(rig.conversation.unsent_replies().empty());
		}
	}

	GIVEN("a value that is not a command (TC-286)") {
		const std::string reply = rig.answer("*0\r\n");

		THEN("the error is answered and the conversation stays open") {
			CHECK(reply.starts_with("-ERR"));
			CHECK_FALSE(rig.conversation.is_over());
			CHECK(rig.answer("PING\r\n") == "+PONG\r\n");
		}
	}
}

SCENARIO("A conversation answers pipelined requests in order",
		 "[net][conversation]") {
	const bool threaded = GENERATE(false, true);
	CAPTURE(threaded);

	GIVEN("a query buffer limit of 64 bytes (TC-281)") {
		Rig rig(threaded, NetLimits{.max_query_buffer = QUERY_LIMIT});

		WHEN("three requests arrive in one read") {
			const std::string replies =
				rig.answer("PING a\r\nPING b\r\nPING c\r\n");

			THEN("one batch answers them in order") {
				CHECK(replies == "$1\r\na\r\n$1\r\nb\r\n$1\r\nc\r\n");
			}

			AND_WHEN("as many bytes arrive again") {
				const std::string again =
					rig.answer("PING d\r\nPING e\r\nPING f\r\n");

				THEN("the first batch's bytes no longer count against the "
					 "limit") {
					CHECK(again == "$1\r\nd\r\n$1\r\ne\r\n$1\r\nf\r\n");
					CHECK_FALSE(rig.conversation.is_over());
					CHECK(rig.guard.count(Violation::QueryBufferFull) == 0);
				}
			}
		}
	}
}

SCENARIO("Inline and threaded conversations answer alike",
		 "[net][conversation]") {
	GIVEN("a stream of requests ending in a protocol error (TC-282)") {
		const std::string stream = std::string("PING one\r\n") +
								   std::string(PING_RESP) +
								   "*2\r\n$3\r\nGET\r\n$1\r\nk\r\n"
								   "PING two\r\n*1\r\n$3\r\nabcXY";

		// What a conversation says of the whole stream when it arrives cut at
		// \p cut: the replies, and whether it ended.
		const auto run = [&](bool threaded, size_t cut) {
			Rig rig(threaded);
			std::string replies = rig.answer(stream.substr(0, cut));
			if (!rig.conversation.is_over())
				replies += rig.answer(stream.substr(cut));
			return std::pair{replies, rig.conversation.is_over()};
		};

		THEN("the same bytes come back whatever the cut") {
			const auto whole = run(false, stream.size());
			CHECK(whole.second);
			for (size_t cut = 1; cut < stream.size(); ++cut) {
				CAPTURE(cut);
				CHECK(run(false, cut) == whole);
				CHECK(run(true, cut) == whole);
			}
		}
	}
}

SCENARIO("A conversation ends when a client breaks a limit",
		 "[net][conversation]") {
	const bool threaded = GENERATE(false, true);
	CAPTURE(threaded);

	GIVEN("a query buffer limit of 64 bytes (TC-283)") {
		Rig rig(threaded, NetLimits{.max_query_buffer = QUERY_LIMIT});
		// A bulk string of 100 bytes, begun: it stays incomplete.
		std::string partial = "*1\r\n$100\r\n";
		partial.resize(QUERY_LIMIT, 'x');

		WHEN("64 bytes are pending") {
			(void)rig.feed(partial);

			THEN("they are within the limit") {
				CHECK_FALSE(rig.conversation.is_over());
				CHECK(rig.guard.count(Violation::QueryBufferFull) == 0);
			}

			AND_WHEN("one more byte arrives") {
				const bool ran = rig.feed("x");

				THEN("it is refused, counted once, and the client is over") {
					CHECK_FALSE(ran);
					CHECK(rig.conversation.is_over());
					CHECK(rig.guard.count(Violation::QueryBufferFull) == 1);
				}
			}
		}
	}

	GIVEN("an output limit of 200 bytes and a value of 100 bytes (TC-284)") {
		Rig rig(threaded, NetLimits{.max_output_buffer = OUTPUT_LIMIT});
		rig.server.run({"SET", "x", std::string(VALUE_SIZE, 'v')});
		std::string requests;
		for (size_t i = 0; i < PIPELINED_GETS; ++i) requests += "GET x\r\n";

		WHEN("ten requests are pipelined") {
			const std::string replies = rig.answer(requests);

			THEN("the replies are dropped, counted once, and the client is "
				 "over") {
				CHECK(replies.empty());
				CHECK(rig.conversation.is_over());
				CHECK(rig.guard.count(Violation::OutputBufferFull) == 1);
			}
		}

		WHEN("only one request is sent") {
			const std::string replies = rig.answer("GET x\r\n");

			THEN("it is answered and the client stays") {
				CHECK(replies.size() == REPLY_SIZE);
				CHECK_FALSE(rig.conversation.is_over());
				CHECK(rig.guard.count(Violation::OutputBufferFull) == 0);
			}
		}
	}

	GIVEN("room for every reply") {
		Rig rig(threaded);
		rig.server.run({"SET", "x", std::string(VALUE_SIZE, 'v')});
		std::string requests;
		for (size_t i = 0; i < PIPELINED_GETS; ++i) requests += "GET x\r\n";

		THEN("all ten run and the client stays (TC-284)") {
			CHECK(rig.answer(requests).size() == PIPELINED_GETS * REPLY_SIZE);
			CHECK_FALSE(rig.conversation.is_over());
		}
	}
}

SCENARIO("A protocol error ends a conversation after the replies before it",
		 "[net][conversation]") {
	const bool threaded = GENERATE(false, true);
	CAPTURE(threaded);
	Rig rig(threaded);

	GIVEN("a valid request, then a malformed one (TC-285)") {
		const std::string replies = rig.answer("PING ok\r\n*1\r\n$3\r\nabcXY");

		THEN("the first is answered, then the error, then it is over") {
			CHECK(replies.starts_with("$2\r\nok\r\n-ERR Protocol error"));
			CHECK(rig.conversation.is_over());
			CHECK(rig.guard.count(Violation::QueryBufferFull) == 0);
			CHECK(rig.guard.count(Violation::OutputBufferFull) == 0);
		}
	}
}

SCENARIO("Timeouts and failures end a conversation", "[net][conversation]") {
	const bool threaded = GENERATE(false, true);
	CAPTURE(threaded);
	Rig rig(threaded);

	GIVEN("a client that has said nothing for 30 seconds (TC-287)") {
		rig.conversation.count_idle_timeout_and_end(30s);

		THEN("it is counted once, and over") {
			CHECK(rig.conversation.is_over());
			CHECK(rig.guard.count(Violation::IdleTimeout) == 1);
		}
	}

	GIVEN("a reply waiting to be sent") {
		// Run, but not taken out: the session has not sent it yet.
		if (rig.feed("PING\r\n")) rig.conversation.execute_ingested_requests();
		REQUIRE(rig.conversation.unsent_replies() == "+PONG\r\n");

		WHEN("the request limit runs out (TC-288)") {
			rig.conversation.count_request_timeout(1s);

			THEN("it is counted, and the replies are left for the work "
				 "still unwinding") {
				CHECK(rig.guard.count(Violation::RequestTimeout) == 1);
				CHECK_FALSE(rig.conversation.is_over());
				CHECK(rig.conversation.unsent_replies() == "+PONG\r\n");
			}

			AND_WHEN("that work has unwound") {
				rig.conversation.drop_replies_and_end();

				THEN("the replies are dropped and the client is over") {
					CHECK(rig.conversation.unsent_replies().empty());
					CHECK(rig.conversation.is_over());
					CHECK(rig.guard.count(Violation::RequestTimeout) == 1);
				}
			}
		}

		WHEN("the batch failed (TC-289)") {
			rig.conversation.drop_replies_and_end();

			THEN("nothing is sent, the client is over, and nothing is "
				 "counted") {
				CHECK(rig.conversation.unsent_replies().empty());
				CHECK(rig.conversation.is_over());
				for (const auto violation :
					 {Violation::QueryBufferFull,
					  Violation::OutputBufferFull,
					  Violation::IdleTimeout,
					  Violation::RequestTimeout})
					CHECK(rig.guard.count(violation) == 0);
			}
		}
	}
}

SCENARIO("A failed read or write is told about as it deserves",
		 "[net][conversation]") {
	const auto failure = [](const std::exception_ptr &error) {
		return connection_error(ConnectionKind::ReadFailed, error);
	};
	const ConnectionError left = failure(std::make_exception_ptr(
		boost::system::system_error(boost::asio::error::eof)));
	const ConnectionError other =
		failure(std::make_exception_ptr(std::runtime_error("boom")));

	GIVEN("the client hung up (TC-290)") {
		THEN("it is not a failure, whether or not the socket is open") {
			CHECK(report_for(left, true) == IoFailureReport::ClientLeft);
			CHECK(report_for(left, false) == IoFailureReport::ClientLeft);
		}
	}

	GIVEN("any other error") {
		THEN("it is a failure while the socket is open, and not news once "
			 "the server closed it") {
			CHECK(report_for(other, true) == IoFailureReport::Failure);
			CHECK(report_for(other, false) ==
				  IoFailureReport::AfterServerClose);
		}
	}
}
