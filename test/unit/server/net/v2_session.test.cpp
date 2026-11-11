#include "server/net/v2/detail/asio_context.hpp"
#include "server/net/v2/detail/session.hpp"
#include "server/services.hpp"
#include "support/temp_file.hpp"

#include <boost/asio/buffer.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/asio/write.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>
#include <stdexec/execution.hpp>

#include <array>
#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

using namespace redis;
using namespace std::chrono_literals;
namespace asio = boost::asio;
using asio::ip::tcp;

namespace {

constexpr std::string_view PING = "*1\r\n$4\r\nPING\r\n";
constexpr std::string_view PONG = "+PONG\r\n";

/// How a client's wait for the server ended.
enum class Heard { Data, Closed, Silence };

/**
 * \brief A v2 session served over a real loopback connection, in-process.
 *
 * Networking needs no special tools: the I/O runs on a thread of the
 * fixture's own io_context, the test plays the client over a real socket, and
 * every wait has a deadline so a hung session fails the test instead of
 * hanging it.
 */
class SessionFixture {
public:
	explicit SessionFixture(std::chrono::seconds idle_timeout = 0s)
		: services_(std::make_shared<Services>(
			  file_.path(), false, std::nullopt,
			  NetLimits{.idle_timeout = idle_timeout})),
		  work_(asio::make_work_guard(io_)),
		  client_(io_),
		  drain_(io_.get_executor()) {
		io_thread_ = std::thread([this] { io_.run(); });

		tcp::acceptor acceptor(io_, {asio::ip::make_address("127.0.0.1"), 0});
		auto accepted =
			acceptor.async_accept(asio::make_strand(io_), asio::use_future);
		client_.connect(acceptor.local_endpoint());
		// Same as the listener: each session on a strand of its own.
		tcp::socket server_side = accepted.get();
		// A small send buffer, so a big reply is only partly taken at once and
		// the session has to send the rest asynchronously.
		boost::system::error_code ignored;
		server_side.set_option(asio::socket_base::send_buffer_size(4096),
							   ignored);
		stdexec::spawn(
			detail::v2::Session::serve(std::move(server_side), services_),
			scope_.get_token());
	}

	SessionFixture(const SessionFixture &)            = delete;
	SessionFixture &operator=(const SessionFixture &) = delete;

	~SessionFixture() {
		scope_.request_stop();
		if (!joined_) join(5s);
		work_.reset();
		io_thread_.join();
	}

	[[nodiscard]] NetGuard &guard() { return services_->guard; }

	void send(std::string_view bytes) {
		asio::write(client_, asio::buffer(bytes.data(), bytes.size()));
	}

	/// Waits up to \p limit for the server to send something or hang up.
	Heard listen(std::chrono::milliseconds limit, std::string &data) {
		std::array<char, 64 * 1024> chunk{};
		std::promise<std::pair<boost::system::error_code, size_t>> done;
		auto result = done.get_future();
		client_.async_read_some(asio::buffer(chunk),
								[&](boost::system::error_code ec, size_t n) {
									done.set_value({ec, n});
								});
		if (result.wait_for(limit) != std::future_status::ready) {
			// Cancelling completes the read handler, which touches `done`.
			asio::post(io_, [this] { client_.cancel(); });
			result.wait();
			return Heard::Silence;
		}
		const auto [ec, n] = result.get();
		if (ec) return Heard::Closed;
		data.assign(chunk.data(), n);
		return Heard::Data;
	}

	/// Reads until \p total bytes have arrived or \p limit passes; whatever
	/// arrived is returned.
	std::string read_exactly(size_t total, std::chrono::milliseconds limit) {
		std::string received;
		const auto deadline = std::chrono::steady_clock::now() + limit;
		while (received.size() < total) {
			const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
				deadline - std::chrono::steady_clock::now());
			if (left <= 0ms) break;
			std::string part;
			if (listen(left, part) != Heard::Data) break;
			received += part;
		}
		return received;
	}

	/// Sends a PING and expects the PONG within a generous deadline.
	bool ping_answered() {
		send(PING);
		std::string data;
		return listen(2s, data) == Heard::Data && data == PONG;
	}

	/// True if every session has finished within \p limit.
	bool join(std::chrono::milliseconds limit) {
		auto joining = std::async(std::launch::async, [this] {
			stdexec::sync_wait(
				stdexec::starts_on(drain_.get_scheduler(), scope_.join()));
		});
		joined_      = joining.wait_for(limit) == std::future_status::ready;
		return joined_;
	}

	/// Cancel every session, as the listener's stop() does.
	void stop() { scope_.request_stop(); }

	void close_client() {
		boost::system::error_code ignored;
		client_.close(ignored);
	}

private:
	test::TempFile file_;
	std::shared_ptr<Services> services_;
	asio::io_context io_;
	asio::executor_work_guard<asio::io_context::executor_type> work_;
	tcp::socket client_;
	detail::AsioContext drain_;
	stdexec::counting_scope scope_;
	std::thread io_thread_;
	bool joined_ = false;
};

} // namespace

SCENARIO("A v2 session answers over a real connection", "[net][v2][session]") {
	GIVEN("a connected client") {
		SessionFixture fixture;

		WHEN("it sends PING") {
			THEN("it is answered, and again on the same connection") {
				CHECK(fixture.ping_answered());
				CHECK(fixture.ping_answered());
			}
		}

		WHEN("the client hangs up") {
			CHECK(fixture.ping_answered());
			fixture.close_client();

			THEN("the session ends by itself") { CHECK(fixture.join(3s)); }
		}

		WHEN("the server is asked to stop") {
			CHECK(fixture.ping_answered());
			fixture.stop();
			std::string data;

			THEN("the connection is closed and the session finishes") {
				CHECK(fixture.listen(3s, data) == Heard::Closed);
				CHECK(fixture.join(3s));
			}
		}
	}
}

SCENARIO("A v2 session closes idle clients", "[net][v2][session][timeout]") {
	GIVEN("a one second idle limit") {
		SessionFixture fixture(1s);

		WHEN("the client says nothing") {
			std::string data;
			const auto heard = fixture.listen(4s, data);

			THEN("the server hangs up and counts the violation") {
				CHECK(heard == Heard::Closed);
				CHECK(fixture.guard().count(Violation::IdleTimeout) == 1);
				CHECK(fixture.join(3s));
			}
		}

		WHEN("the client keeps talking, each request inside the limit") {
			bool all_answered = true;
			for (int i = 0; i < 4; ++i) {
				// 4 x 0.6 s outlives the 1 s limit: the timer must be
				// re-armed every round, not run from the connection's start.
				std::this_thread::sleep_for(600ms);
				all_answered = all_answered && fixture.ping_answered();
			}

			THEN("it is never closed for idling") {
				CHECK(all_answered);
				CHECK(fixture.guard().count(Violation::IdleTimeout) == 0);
			}
		}
	}

	GIVEN("no idle limit") {
		SessionFixture fixture;

		WHEN("the client stays quiet for a while") {
			std::string data;
			const auto heard = fixture.listen(1500ms, data);

			THEN("it is left alone and still served") {
				CHECK(heard == Heard::Silence);
				CHECK(fixture.guard().count(Violation::IdleTimeout) == 0);
				CHECK(fixture.ping_answered());
			}
		}
	}
}

SCENARIO("A reply larger than the socket buffer arrives whole",
		 "[net][v2][session]") {
	GIVEN("a connected client that stored a 4 MiB value") {
		SessionFixture fixture;
		const std::string value(size_t{4} << 20, 'x');
		fixture.send(fmt::format(
			"*3\r\n$3\r\nSET\r\n$1\r\nk\r\n${}\r\n{}\r\n", value.size(), value));
		REQUIRE(fixture.read_exactly(5, 10s) == "+OK\r\n");

		WHEN("it reads the value back") {
			fixture.send("*2\r\n$3\r\nGET\r\n$1\r\nk\r\n");
			const std::string expected =
				fmt::format("${}\r\n{}\r\n", value.size(), value);

			// On Linux and macOS the socket takes only part of the reply at
			// once, so the rest goes through async_write. Windows loopback
			// takes all 4 MiB in one write_some even with a small send
			// buffer, so there this checks only that the reply arrives whole.
			THEN("the socket takes part of it at once and the rest follows") {
				const std::string reply =
					fixture.read_exactly(expected.size(), 20s);
				CHECK(reply.size() == expected.size());
				CHECK(reply == expected);
				CHECK(fixture.ping_answered());
			}
		}
	}
}
