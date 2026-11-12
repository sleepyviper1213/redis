#pragma once
#include <fmt/format.h>
#include <hiredis/hiredis.h>

#include <cstdlib>
#include <memory>
#include <string>

struct TestResult {
	bool ok = false;
	std::string value;
	std::string error;
};

class RedisContext {
public:
	RedisContext() = default;

	bool connect(const std::string &host, int port) {
		ctx_.reset(redisConnect(host.c_str(), port));
		return ctx_ && !ctx_->err;
	}

	[[nodiscard]] bool valid() const { return ctx_ && !ctx_->err; }

	[[nodiscard]] const char *error() const {
		return ctx_ ? ctx_->errstr : "No context";
	}

	friend class ReplyPtr;

private:
	struct Deleter {
		void operator()(redisContext *p) const { redisFree(p); }
	};

	std::unique_ptr<redisContext, Deleter> ctx_;
};

class ReplyPtr {
public:
	ReplyPtr() = default;

	template <typename... Args>
	void command(const RedisContext &context, const char *format, Args... args) {
		rep_.reset(reinterpret_cast<redisReply *>(
			redisCommand(context.ctx_.get(), format, args...)));
	}

	[[nodiscard]] bool valid() const { return rep_ != nullptr; }

	[[nodiscard]] redisReply *get() const { return rep_.get(); }

private:
	struct Deleter {
		void operator()(redisReply *p) const { freeReplyObject(p); }
	};

	std::unique_ptr<redisReply, Deleter> rep_;
};

/// The server's port: REDIS_TEST_PORT (set by scripts/test_server.sh), else
/// 6380.
inline int test_port() {
	const char *env = std::getenv("REDIS_TEST_PORT"); // NOLINT(concurrency-mt-unsafe): read before any thread starts
	return env != nullptr ? std::atoi(env) : 6380;
}

class TestClient {
public:
	TestClient(const std::string &host = "127.0.0.1", int port = test_port()) {
		if (!context_.connect(host, port)) error_ = context_.error();
	}

	[[nodiscard]] bool is_connected() const {
		return context_.valid() && error_.empty();
	}

	[[nodiscard]] const std::string &last_error() const { return error_; }

	// raw command
	template <typename... Args>
	TestResult send(const char *format, Args... args) {
		if (!is_connected())
			return {.ok    = false,
					.value = "",
					.error = fmt::format("Not connected: {}", error_)};

		ReplyPtr reply;
		reply.command(context_, format, args...);
		if (!reply.valid())
			return {.ok = false, .value = "", .error = "NULL reply"};

		return formatReply(reply.get());
	}

private:
	static TestResult formatReply(redisReply *reply) {
		TestResult res;
		switch (reply->type) {
		case REDIS_REPLY_STRING:
			res.ok = true;
			res.value.assign(reply->str, reply->len);
			break;
		case REDIS_REPLY_STATUS:
			res.ok    = true;
			res.value = reply->str ? reply->str : "";
			break;
		case REDIS_REPLY_INTEGER:
			res.ok    = true;
			res.value = fmt::to_string(reply->integer);
			break;
		case REDIS_REPLY_NIL:
			res.ok    = true;
			res.value = "(nil)";
			break;
		case REDIS_REPLY_ERROR:
			res.ok    = false;
			res.error = reply->str ? reply->str : "ERR";
			break;
		default:
			res.ok    = false;
			res.error = "Unknown reply type";
			break;
		}
		return res;
	}

	RedisContext context_;
	std::string error_;
};
