#include "server_listener.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <hiredis/hiredis.h>

#include <cstdlib>
#include <string>
using Catch::Matchers::Equals;
TEST_CASE("Redis PING-PONG once", "[server][ping_pong]") {
	redisContext *c = redisConnect("127.0.0.1", 6379);
	REQUIRE(c != nullptr);
	REQUIRE(c->err == 0);

	redisReply *reply = (redisReply *)redisCommand(c, "PING");
	REQUIRE(reply != nullptr);
	REQUIRE(reply->type == REDIS_REPLY_STATUS);
		REQUIRE_THAT(reply->str,Equals( "PONG"));

	freeReplyObject(reply);
	redisFree(c);
}

TEST_CASE("Redis PING-PONG multiple", "[server][ping_pong]") {
	redisContext *c = redisConnect("127.0.0.1", 6379);
	REQUIRE(c != nullptr);
	REQUIRE(c->err == 0);

	for (int i = 0; i < 3; i++) {
		redisReply *reply = (redisReply *)redisCommand(c, "PING");
		REQUIRE(reply != nullptr);
			REQUIRE_THAT(reply->str,Equals( "PONG"));
		freeReplyObject(reply);
	}

	redisFree(c);
}

TEST_CASE("Redis PING with multiple clients", "[server][ping_pong]") {
	redisContext *c1 = redisConnect("127.0.0.1", 6379);
	redisContext *c2 = redisConnect("127.0.0.1", 6379);
	REQUIRE(c1 != nullptr);
	REQUIRE(c2 != nullptr);
	REQUIRE(c1->err == 0);
	REQUIRE(c2->err == 0);

	redisReply *r1 = (redisReply *)redisCommand(c1, "PING");
	REQUIRE_THAT(r1->str,Equals( "PONG"));
	freeReplyObject(r1);

	redisReply *r2 = (redisReply *)redisCommand(c2, "PING");
	REQUIRE_THAT(r2->str,Equals( "PONG"));
	freeReplyObject(r2);

	redisFree(c1);
	redisFree(c2);
}
