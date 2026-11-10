#include "commands.fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <new>
#include <string>
#include <string_view>

using namespace redis;

namespace {
// A command that runs out of memory while it works out its reply.
resp::Value runs_out_of_memory(CommandContext & /*ctx*/, Args /*args*/) {
	throw std::bad_alloc();
}

constexpr std::array EXTRA{CommandInfo{"OOMTEST", runs_out_of_memory, 1}};
} // namespace

SCENARIO("A command that runs out of memory", "[commands][handler]") {
	GIVEN("a handler with a command that runs out of memory") {
		test::Server server;
		const CommandHandler handler{EXTRA};
		// Replies of earlier requests in the same pipelined batch.
		std::string replies = "+OK\r\n";

		WHEN("it is executed") {
			const std::array<std::string_view, 1> argv{"OOMTEST"};
			handler.execute(server.ctx, Args{argv}, replies);

			THEN("it is answered with an error, earlier replies kept") {
				CHECK(replies ==
					  "+OK\r\n-OOM command not allowed: out of memory\r\n");
			}

			AND_THEN("the next request in the batch runs normally") {
				const std::array<std::string_view, 1> ping{"PING"};
				handler.execute(server.ctx, Args{ping}, replies);
				CHECK(replies ==
					  "+OK\r\n-OOM command not allowed: out of memory\r\n"
					  "+PONG\r\n");
			}
		}
	}
}
