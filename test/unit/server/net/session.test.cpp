#include "server/net/detail/session.hpp"

#include "server/net/detail/sessions.hpp"
#include "server/services.hpp"
#include "support/loopback.hpp"
#include "support/temp_file.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/use_future.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>
#include <stdexec/execution.hpp>

#include <chrono>
#include <concepts>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

using namespace redis;
using namespace std::chrono_literals;
namespace asio = boost::asio;
using asio::ip::tcp;
using test::Heard;

namespace {

/**
 * \brief One session served over a real loopback connection, in-process:
 * the server side runs on the fixture's I/O thread, and the test is the
 * client.
 */
class SessionFixture {
public:
	/// With \p threaded, commands run on a keyspace thread of their own (as
	/// with --io-threads above 1); otherwise inline on the session's thread.
	explicit SessionFixture(NetLimits limits = {}, bool threaded = false)
		: services_(std::make_shared<Services>(file_.path(), threaded,
											   std::nullopt, limits)),
		  sessions_(context_.executor()),
		  client(connect_and_serve()) {}

	SessionFixture(const SessionFixture &)            = delete;
	SessionFixture &operator=(const SessionFixture &) = delete;

	~SessionFixture() {
		sessions_.stop();
		if (!drained_) drain_within(5s);
	}

	[[nodiscard]] NetGuard &guard() { return services_->guard; }

	/// Cancels the session, as the listener's stop() does.
	void stop() { sessions_.stop(); }

	/// Requires the session to finish within \p limit; see
	/// test::require_finishes_within.
	void drain_within(std::chrono::milliseconds limit) {
		test::require_finishes_within(sessions_.drained(), limit, "The session");
		drained_ = true;
	}

	/// Runs \p fn on the keyspace thread, blocking until it has: how a test
	/// holds that thread busy, or reads the keyspace where it lives.
	/// \tparam F The callable's type.
	template <std::invocable F>
	auto on_keyspace(F fn) {
		auto result = stdexec::sync_wait(services_->keyspace.run_on(
			std::move(fn), stdexec::inline_scheduler{}));
		if constexpr (!std::is_void_v<std::invoke_result_t<F>>)
			return std::get<0>(result.value());
	}

	/// How many keys the keyspace holds, read on its own thread.
	size_t keys() {
		return on_keyspace([this] { return services_->db.size(); });
	}

private:
	/// Connects a client to a session served by this fixture, as the listener
	/// would serve it: on a strand of its own.
	test::Client connect_and_serve() {
		tcp::acceptor acceptor(context_.io(),
							   {asio::ip::make_address("127.0.0.1"), 0});
		auto accepted = acceptor.async_accept(asio::make_strand(context_.io()),
											  asio::use_future);
		test::Client connected(context_.io(), acceptor.local_endpoint().port());
		tcp::socket server_side = accepted.get();
		// A small send buffer, so a big reply is only partly taken at once and
		// the session has to send the rest asynchronously.
		boost::system::error_code ignored;
		server_side.set_option(asio::socket_base::send_buffer_size(4096),
							   ignored);
		sessions_.serve(std::move(server_side), services_);
		return connected;
	}

	test::TempFile file_;
	std::shared_ptr<Services> services_;
	test::RunningContext context_;
	detail::Sessions sessions_;
	bool drained_ = false;

public:
	/// The test's end of the connection; declared last, so closed first.
	test::Client client;
};

} // namespace

SCENARIO("A session answers over a real connection", "[net][session]") {
	GIVEN("a connected client") {
		SessionFixture fixture;

		WHEN("it sends PING") {
			THEN("it is answered, and again on the same connection") {
				CHECK(fixture.client.ping_answered());
				CHECK(fixture.client.ping_answered());
			}
		}

		WHEN("the client hangs up") {
			CHECK(fixture.client.ping_answered());
			fixture.client.close();

			THEN("the session ends by itself") { fixture.drain_within(3s); }
		}

		WHEN("the server is asked to stop") {
			CHECK(fixture.client.ping_answered());
			fixture.stop();
			std::string data;

			THEN("the connection is closed and the session finishes") {
				CHECK(fixture.client.listen(3s, data) == Heard::Closed);
				fixture.drain_within(3s);
			}
		}
	}
}

SCENARIO("A session closes idle clients", "[net][session][timeout]") {
	GIVEN("a one second idle limit") {
		SessionFixture fixture(NetLimits{.idle_timeout = 1s});

		WHEN("the client says nothing") {
			std::string data;
			const auto heard = fixture.client.listen(4s, data);

			THEN("the server hangs up and counts the violation") {
				CHECK(heard == Heard::Closed);
				CHECK(fixture.guard().count(Violation::IdleTimeout) == 1);
				fixture.drain_within(3s);
			}
		}

		WHEN("the client keeps talking, each request inside the limit") {
			bool all_answered = true;
			for (int i = 0; i < 4; ++i) {
				// 4 x 0.6 s outlives the 1 s limit: the timer must be
				// re-armed every round, not run from the connection's start.
				std::this_thread::sleep_for(600ms);
				all_answered = all_answered && fixture.client.ping_answered();
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
			const auto heard = fixture.client.listen(1500ms, data);

			THEN("it is left alone and still served") {
				CHECK(heard == Heard::Silence);
				CHECK(fixture.guard().count(Violation::IdleTimeout) == 0);
				CHECK(fixture.client.ping_answered());
			}
		}
	}
}

SCENARIO("A session closes clients whose requests are not answered in time",
		 "[net][session][timeout]") {
	GIVEN("a one second request limit") {
		SessionFixture fixture(NetLimits{.request_timeout = 1s});

		WHEN("the client is answered in time") {
			THEN("nothing is counted") {
				CHECK(fixture.client.ping_answered());
				CHECK(fixture.client.ping_answered());
				CHECK(fixture.guard().count(Violation::RequestTimeout) == 0);
			}
		}
	}

	GIVEN("a one second request limit, and a keyspace thread busy for two") {
		SessionFixture fixture(NetLimits{.request_timeout = 1s}, true);
		std::jthread busy(
			[&fixture] { fixture.on_keyspace([] { std::this_thread::sleep_for(2s); }); });
		std::this_thread::sleep_for(100ms); // the busy job is queued first

		WHEN("the client sends a write, which waits behind that job") {
			fixture.client.send("*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$1\r\nv\r\n");
			std::string data;
			const auto heard = fixture.client.listen(4s, data);

			THEN("the server hangs up and counts it, and the write never runs") {
				CHECK(heard == Heard::Closed);
				CHECK(fixture.guard().count(Violation::RequestTimeout) == 1);
				fixture.drain_within(3s);
				// The delivery guarantee: a batch still queued when the limit
				// runs out is dropped, not applied late.
				CHECK(fixture.keys() == 0);
			}
		}
	}

#ifndef _WIN32
	// Windows loopback takes even 128 MiB in one write_some, unread, so a
	// write never stalls there; the finite socket buffers of Linux and macOS
	// do stall it.
	GIVEN("a one second request limit, and a stored 4 MiB value") {
		SessionFixture fixture(NetLimits{.request_timeout = 1s});
		const std::string value(size_t{4} << 20, 'x');
		fixture.client.send(fmt::format("*3\r\n$3\r\nSET\r\n$1\r\nk\r\n${}\r\n{}\r\n",
								 value.size(),
								 value));
		REQUIRE(fixture.client.read_exactly(5, 10s) == "+OK\r\n");

		WHEN("the client asks for more than the connection holds, unread") {
			// 32 GETs of the value: 128 MiB of replies, more than the socket
			// buffers at either end, so the write stalls until the limit gives
			// up on it.
			std::string gets;
			for (int i = 0; i < 32; ++i)
				gets += "*2\r\n$3\r\nGET\r\n$1\r\nk\r\n";
			fixture.client.send(gets);

			THEN("the server abandons the write, hangs up and counts it") {
				fixture.drain_within(6s);
				CHECK(fixture.guard().count(Violation::RequestTimeout) == 1);
			}
		}
	}
#endif

	GIVEN("no request limit") {
		SessionFixture fixture;

		WHEN("the client is served") {
			THEN("nothing is counted") {
				CHECK(fixture.client.ping_answered());
				CHECK(fixture.guard().count(Violation::RequestTimeout) == 0);
			}
		}
	}
}

SCENARIO("A reply larger than the socket buffer arrives whole",
		 "[net][session]") {
	GIVEN("a connected client that stored a 4 MiB value") {
		SessionFixture fixture;
		const std::string value(size_t{4} << 20, 'x');
		fixture.client.send(fmt::format("*3\r\n$3\r\nSET\r\n$1\r\nk\r\n${}\r\n{}\r\n",
								 value.size(),
								 value));
		REQUIRE(fixture.client.read_exactly(5, 10s) == "+OK\r\n");

		WHEN("it reads the value back") {
			fixture.client.send("*2\r\n$3\r\nGET\r\n$1\r\nk\r\n");
			const std::string expected =
				fmt::format("${}\r\n{}\r\n", value.size(), value);

			// On Linux and macOS the socket takes only part of the reply at
			// once, so the rest goes through async_write. Windows loopback
			// takes all 4 MiB in one write_some even with a small send
			// buffer, so there this checks only that the reply arrives whole.
			THEN("the socket takes part of it at once and the rest follows") {
				const std::string reply =
					fixture.client.read_exactly(expected.size(), 20s);
				CHECK(reply.size() == expected.size());
				CHECK(reply == expected);
				CHECK(fixture.client.ping_answered());
			}
		}
	}
}
