#include "common/resp/value.hpp"
#include "server/commands/reply.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <string>

using namespace redis;
using resp::Value;

namespace {
std::string encoded(const Value &value) {
	std::string out;
	resp::encode_to(out, value);
	return out;
}
} // namespace

TEST_CASE("ReplyWriter writes what resp::encode_to writes", "[reply]") {
	std::string out;
	ReplyWriter writer(out);

	SECTION("simple strings and errors") {
		writer.ok();
		CHECK(out == encoded(Value::from_simple_string("OK")));
		out.clear();
		writer.simple("PONG");
		CHECK(out == encoded(Value::from_simple_string("PONG")));
		out.clear();
		writer.error("ERR boom");
		CHECK(out == encoded(Value::from_simple_error("ERR boom")));
	}

	SECTION("integers, including the extremes") {
		for (const int64_t n : {int64_t{0},
								int64_t{-1},
								int64_t{1234567},
								std::numeric_limits<int64_t>::min(),
								std::numeric_limits<int64_t>::max()}) {
			out.clear();
			writer.integer(n);
			CHECK(out == encoded(Value::from_integer(n)));
		}
	}

	SECTION("bulk strings, empty and containing CRLF") {
		for (const std::string &text : {std::string{},
									   std::string{"x"},
									   std::string{"a\r\nb"},
									   std::string(100000, 'z')}) {
			out.clear();
			writer.bulk(text);
			CHECK(out == encoded(Value::from_bulk_string(text)));
		}
	}

	SECTION("nil") {
		writer.nil();
		CHECK(out == encoded(Value::from_null()));
	}

	SECTION("arrays") {
		writer.array(3);
		writer.bulk("a");
		writer.nil();
		writer.integer(7);
		CHECK(out == encoded(Value::from_array({Value::from_bulk_string("a"),
												Value::from_null(),
												Value::from_integer(7)})));

		out.clear();
		writer.array(0);
		CHECK(out == encoded(Value::from_array({})));
	}
}

TEST_CASE("ReplyWriter appends to what is already in the buffer", "[reply]") {
	std::string out = "+OK\r\n";
	ReplyWriter writer(out);
	writer.integer(1);
	CHECK(out == "+OK\r\n:1\r\n");
}

TEST_CASE("A simple string or error cannot carry a line break", "[reply][security]") {
	std::string out;
	ReplyWriter writer(out);

	SECTION("CR and LF become spaces") {
		writer.error("ERR unknown command 'a\r\n+INJECTED'");
		CHECK(out == "-ERR unknown command 'a  +INJECTED'\r\n");
	}
	SECTION("a lone LF or CR too, anywhere in the text") {
		writer.simple("\nfirst\rsecond\n");
		CHECK(out == "+ first second \r\n");
	}
	SECTION("text without one is unchanged") {
		writer.simple("PONG");
		CHECK(out == "+PONG\r\n");
	}
}
