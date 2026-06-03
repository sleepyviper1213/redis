#include "redis_server.hpp"

#include "tcp_server.hpp"

#include <boost/asio/co_spawn.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/unordered_map.hpp>
#include <fmt/std.h>

namespace redis {
RedisServer::RedisServer(std::string host, uint_least16_t port)
	: host_(std::move(host)),
	  port_(port),
	  io_(),
	  signals_(io_, SIGINT, SIGTERM),
	  db_(0) {}

void RedisServer::start() {
	logger_->info("Server initialized");
	auto server = TcpListener::from(io_, host_, port_, db_);

	signals_.async_wait([&](auto, auto last_sig_received) {
		logger_->info("Received {}", strsignal(last_sig_received));
		server.stop();
		stop();
	});

	boost::asio::co_spawn(io_, server.start(), [](std::exception_ptr p) {
		if (p) std::rethrow_exception(p);
	});
	io_.run();
}

void RedisServer::save_db() {
	logger_->info("Saving the final RDB snapshot before exiting");

	db_.save();
}

void RedisServer::stop() noexcept {
	io_.stop();
	logger_->info("Redis is now ready to exit");
}

bool RedisServer::is_running() const noexcept { return !io_.stopped(); }

void RedisServer::save_snapshot(const std::filesystem::path &file) const {
	/*std::ofstream os(file, std::ios::binary);
	if (!os) {
		logger_->error("Failed to open snapshot file: {}", file);
		return;
	}
	cereal::BinaryOutputArchive archive(os);
	archive(db_); // assumes Database has serialize() defined
	logger_->info("Snapshot saved to {}", file);*/
}

void RedisServer::load_snapshot(const std::filesystem::path &file) {
	/*std::ifstream is(file, std::ios::binary);
	if (!is) {
		logger_->warn("Snapshot file not found: {}", file);
		return;
	}
	cereal::BinaryInputArchive archive(is);
	archive(db_);
	logger_->info("Snapshot loaded from {}", file);*/
}
} // namespace redis
