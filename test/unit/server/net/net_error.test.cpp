#include "server/net/net_error.hpp"
#include "server/persistence/persist_error.hpp"
#include "server/server_error.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <string>
#include <system_error>

using namespace redis;

namespace {
const std::error_code REFUSED =
	std::make_error_code(std::errc::connection_refused);
} // namespace

SCENARIO("A listen failure names the host, the port and the reason",
		 "[net_error]") {
	GIVEN("a host that is not an address") {
		const ListenError error{.kind   = ListenKind::InvalidHost,
								.cause  = REFUSED,
								.quoted = "not-an-ip"};
		THEN("the host is quoted") {
			CHECK(fmt::format("{}", error) ==
				  "invalid host 'not-an-ip': " + REFUSED.message());
		}
	}
	GIVEN("a port that cannot be taken") {
		const ListenError error{.kind   = ListenKind::CannotListen,
								.cause  = REFUSED,
								.at     = 6379,
								.quoted = "127.0.0.1"};
		THEN("host and port are in the sentence") {
			CHECK(fmt::format("{}", error) ==
				  "cannot listen on 127.0.0.1:6379: " + REFUSED.message());
		}
	}
}

SCENARIO("A connection failure says what failed", "[net_error]") {
	const auto text = [](const ConnectionError &error) {
		return fmt::format("{}", error);
	};

	GIVEN("failures that come from the system") {
		THEN("the reason follows the operation") {
			CHECK(text({.kind = ConnectionKind::ReadFailed, .cause = REFUSED}) ==
				  "Read: " + REFUSED.message());
			CHECK(text({.kind = ConnectionKind::WriteFailed, .cause = REFUSED}) ==
				  "Write: " + REFUSED.message());
			CHECK(text({.kind = ConnectionKind::ExecuteFailed, .cause = REFUSED}) ==
				  "Execute: " + REFUSED.message());
			CHECK(text({.kind = ConnectionKind::AcceptFailed, .cause = REFUSED}) ==
				  "Accept failed: " + REFUSED.message());
		}
	}
	GIVEN("failures known only by an exception's text") {
		THEN("the text stands in for the reason") {
			CHECK(text({.kind   = ConnectionKind::ConnectionFailed,
						.quoted = "boom"}) == "Connection: boom");
			CHECK(text({.kind = ConnectionKind::ReadFailed, .quoted = "boom"}) ==
				  "Read: boom");
			CHECK(text({.kind = ConnectionKind::ExecuteFailed, .quoted = "boom"}) ==
				  "Execute: boom");
			CHECK(text({.kind = ConnectionKind::AcceptFailed, .quoted = "boom"}) ==
				  "Accept failed: boom");
			CHECK(text({.kind   = ConnectionKind::AcceptLoopFailed,
						.quoted = "boom"}) == "Accept loop failed: boom");
		}
		THEN("a session failure with no text is just that") {
			CHECK(text({.kind = ConnectionKind::SessionFailed}) == "Session failed");
			CHECK(text({.kind = ConnectionKind::SessionFailed, .quoted = "boom"}) ==
				  "Session failed: boom");
		}
	}
}

SCENARIO("A server error shows the error that caused it",
		 "[net_error][server_error]") {
	GIVEN("a startup that failed to load, to listen, or in a worker") {
		const persistence::PersistError persist{
			.kind = persistence::PersistKind::InUse, .path = "d"};
		const ListenError listen{.kind   = ListenKind::InvalidHost,
								 .cause  = REFUSED,
								 .quoted = "h"};

		THEN("the source's sentence is the message") {
			CHECK(fmt::format("{}",
							  ServerError{.kind   = ServerKind::LoadFailed,
										  .source = persist}) ==
				  "d is in use by another server; refusing to share it");
			CHECK(fmt::format("{}",
							  ServerError{.kind   = ServerKind::ListenFailed,
										  .source = listen}) ==
				  "invalid host 'h': " + REFUSED.message());
			CHECK(fmt::format("{}",
							  ServerError{.kind   = ServerKind::WorkerFailed,
										  .detail = "died"}) == "died");
		}
		THEN("a failed final save says so before the cause") {
			CHECK(fmt::format("{}",
							  ServerError{.kind   = ServerKind::FinalSaveFailed,
										  .source = persist}) ==
				  "final snapshot failed: d is in use by another server; "
				  "refusing to share it");
		}
	}
}
