#include "common/resp/value.hpp"
#include "server/commands/command.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <limits>
#include <string>

using namespace redis;
using resp::Value;

TEST_CASE("The shared replies encode as RESP", "[reply]") {
	CHECK(fmt::format("{:e}", reply::ok()) == "+OK\r\n");
	CHECK(fmt::format("{:e}", reply::simple("PONG")) == "+PONG\r\n");
	CHECK(fmt::format("{:e}", reply::error("ERR boom")) == "-ERR boom\r\n");
	CHECK(fmt::format("{:e}", reply::nil()) == "$-1\r\n");
	CHECK(fmt::format("{:e}", reply::bulk("a\r\nb")) == "$4\r\na\r\nb\r\n");
	CHECK(fmt::format("{:e}", reply::bulk("")) == "$0\r\n\r\n");
	CHECK(fmt::format("{:e}", reply::array({})) == "*0\r\n");
	CHECK(fmt::format("{:e}",
					  reply::integer(std::numeric_limits<int64_t>::min())) ==
		  ":-9223372036854775808\r\n");
}

TEST_CASE("An array encodes its elements in order", "[reply]") {
	Value::Array elements;
	elements.push_back(reply::bulk("a"));
	elements.push_back(reply::nil());
	elements.push_back(reply::integer(7));
	CHECK(fmt::format("{:e}", reply::array(std::move(elements))) ==
		  "*3\r\n$1\r\na\r\n$-1\r\n:7\r\n");
}

TEST_CASE("A simple string or error cannot carry a line break",
		  "[reply][security]") {
	SECTION("CR and LF become spaces") {
		CHECK(
			fmt::format("{:e}",
						reply::error("ERR unknown command 'a\r\n+INJECTED'")) ==
			"-ERR unknown command 'a  +INJECTED'\r\n");
	}
	SECTION("a lone LF or CR too, anywhere in the text") {
		CHECK(fmt::format("{:e}", reply::simple("\nfirst\rsecond\n")) ==
			  "+ first second \r\n");
	}
	SECTION("text without one is unchanged") {
		CHECK(fmt::format("{:e}", reply::simple("PONG")) == "+PONG\r\n");
	}
}
