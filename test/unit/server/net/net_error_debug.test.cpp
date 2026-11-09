#include "server/net/net_error.hpp"
#include "server/persistence/persist_error.hpp"
#include "server/server_error.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

using namespace redis;

SCENARIO("Net errors have Display and Debug forms", "[net_error][debug]") {
	GIVEN("a listen failure") {
		const ListenError error{.kind   = ListenKind::CannotListen,
								.at     = 6379,
								.quoted = "127.0.0.1"};
		THEN("Debug names the kind and the fields that are set") {
			CHECK(fmt::format("{:?}", error) ==
				  R"(ListenKind::CannotListen { at: 6379, quoted: "127.0.0.1" })");
		}
	}
	GIVEN("a connection failure with nothing to add") {
		THEN("Debug is just the kind") {
			CHECK(fmt::format("{:?}",
							  ConnectionError{.kind = ConnectionKind::ReadFailed}) ==
				  "ConnectionKind::ReadFailed");
		}
	}
}

SCENARIO("A server error's Debug form nests its source", "[server_error][debug]") {
	GIVEN("a startup that failed to load") {
		const ServerError error{
			.kind   = ServerKind::LoadFailed,
			.source = persistence::PersistError{.kind = persistence::PersistKind::InUse,
												.path = "d"}};

		THEN("{} is the source's sentence") {
			CHECK(fmt::format("{}", error) ==
				  "d is in use by another server; refusing to share it");
		}
		THEN("{:?} shows the source in its own Debug form") {
			CHECK(fmt::format("{:?}", error) ==
				  R"(ServerKind::LoadFailed { source: PersistKind::InUse { path: "d" } })");
		}
		THEN("{:#?} indents the source under its field") {
			CHECK(fmt::format("{:#?}", error) ==
				  "ServerKind::LoadFailed {\n"
				  "    source: PersistKind::InUse {\n"
				  "        path: \"d\",\n"
				  "    },\n"
				  "}");
		}
	}
	GIVEN("a worker that died") {
		const ServerError error{.kind = ServerKind::WorkerFailed, .detail = "boom"};
		THEN("the detail is quoted") {
			CHECK(fmt::format("{:?}", error) ==
				  R"(ServerKind::WorkerFailed { detail: "boom" })");
		}
	}
}
