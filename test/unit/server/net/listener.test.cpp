#include "server/net/listener.hpp"

#include "core/stdexec_ext/supervise.hpp"
#include "server/net/detail/listener_impl.tpp" // instantiates the failing one
#include "server/services.hpp"
#include "support/temp_file.hpp"

#include <boost/asio/error.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/write.hpp>
#include <boost/system/system_error.hpp>
#include <catch2/catch_test_macros.hpp>
#include <exec/variant_sender.hpp>
#include <stdexec/execution.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <future>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using namespace redis;
using namespace std::chrono_literals;
namespace asio = boost::asio;
using asio::ip::tcp;

namespace {

constexpr std::string_view PING = "*1\r\n$4\r\nPING\r\n";
constexpr std::string_view PONG = "+PONG\r\n";

/// Which accepts a FailingAcceptor fails, shared by the test and the acceptor.
struct FaultPlan {
	/// How many of the next accepts fail; counts down.
	std::atomic<int> failures = 0;
	/// How many accepts were failed so far.
	std::atomic<int> injected = 0;
	/// What each failed accept says: running out of descriptors, the
	/// transient failure the loop is built to survive.
	boost::system::error_code error = asio::error::no_descriptors;
};

/**
 * \brief A real acceptor whose next accepts fail on demand.
 *
 * It accepts real connections, so a client still connects as usual; while
 * FaultPlan::failures is above zero each async_accept fails at once instead,
 * with FaultPlan::error, and the client waits in the backlog.
 */
class FailingAcceptor : public tcp::acceptor {
public:
	FailingAcceptor(asio::strand<asio::any_io_executor> strand,
					std::shared_ptr<FaultPlan> plan)
		: tcp::acceptor(std::move(strand)), plan_(std::move(plan)) {}

	/**
	 * \brief The real accept, or a failure if one is due.
	 * \tparam Executor The type of the executor for the accepted socket.
	 * \tparam Token The completion token's type: use_sender.
	 * \return A sender of the accepted socket; its type is deduced, a
	 *         variant_sender of the real accept and of the failure.
	 */
	template <class Executor, class Token>
	stdexec::sender auto async_accept(Executor &&ex, Token &&token) {
		using Real =
			decltype(tcp::acceptor::async_accept(std::forward<Executor>(ex),
												 std::forward<Token>(token)));
		using Failed = decltype(stdexec::just_error(std::exception_ptr{}));
		using Result = exec::variant_sender<Real, Failed>;

		int due = plan_->failures.load();
		while (due > 0 &&
			   !plan_->failures.compare_exchange_weak(due, due - 1)) {}
		if (due > 0) {
			++plan_->injected;
			return Result(stdexec::just_error(std::make_exception_ptr(
				boost::system::system_error(plan_->error))));
		}
		return Result(tcp::acceptor::async_accept(std::forward<Executor>(ex),
												  std::forward<Token>(token)));
	}

private:
	std::shared_ptr<FaultPlan> plan_;
};

/**
 * \brief A listener on an ephemeral loopback port, served by \p threads
 * I/O threads. Every wait has a deadline so a hang fails the test.
 *
 * \tparam A The listener's acceptor: tcp::acceptor, as in the server, or
 *         FailingAcceptor to inject failed accepts.
 */
template <Acceptor A>
class BasicListenerFixture {
public:
	/// \p plan is the FailingAcceptor's; unused with tcp::acceptor.
	explicit BasicListenerFixture(unsigned threads                = 1,
								  std::shared_ptr<FaultPlan> plan = {})
		// Several I/O threads need the keyspace on a thread of its own; with
		// one, commands may run inline on it.
		: services_(std::make_shared<Services>(file_.path(), threads > 1,
											   std::nullopt, NetLimits{})),
		  work_(asio::make_work_guard(io_)),
		  listener_(make_listener(io_, services_, threads, std::move(plan))) {
		const auto bound = listener_.listen("127.0.0.1", 0);
		REQUIRE(bound.has_value());
		// As the server does, but a failure is recorded, not fatal: the tests
		// check that stopping ends the loop without one.
		stdexec::spawn(
			stdexec_ext::supervised<stdexec_ext::FailPolicy::Isolate>(
				listener_.run(),
				[this](const std::exception_ptr &) noexcept {
					loop_failed_ = true;
				}),
			loop_scope_.get_token());
		for (unsigned i = 0; i < threads; ++i)
			threads_.emplace_back([this] { io_.run(); });
	}

	BasicListenerFixture(const BasicListenerFixture &)            = delete;
	BasicListenerFixture &operator=(const BasicListenerFixture &) = delete;

	~BasicListenerFixture() {
		listener_.stop();
		if (!drained_) drain(5s);
		work_.reset();
		for (auto &t : threads_) t.join();
	}

	/// A connected client socket, or a failed one if nothing accepts.
	tcp::socket connect() {
		tcp::socket client(io_);
		client.connect({asio::ip::make_address("127.0.0.1"), listener_.port()});
		return client;
	}

	/// PING on \p client; true if the PONG arrives.
	static bool ping(tcp::socket &client) {
		asio::write(client, asio::buffer(PING.data(), PING.size()));
		std::array<char, PONG.size()> reply{};
		boost::system::error_code ec;
		asio::read(client, asio::buffer(reply), ec);
		return !ec && std::string_view(reply.data(), reply.size()) == PONG;
	}

	/// True if the client sees the server hang up within \p limit.
	bool hung_up(tcp::socket &client, std::chrono::milliseconds limit) {
		auto done = std::async(std::launch::async, [&client] {
			std::array<char, 16> chunk{};
			boost::system::error_code ec;
			client.read_some(asio::buffer(chunk), ec);
			return static_cast<bool>(ec);
		});
		if (done.wait_for(limit) == std::future_status::ready)
			return done.get();
		boost::system::error_code ignored;
		client.close(ignored); // unblocks the reader
		done.wait();
		return false;
	}

	void stop() { listener_.stop(); }

	/// Whether the accept loop ended with an error rather than by stopping.
	[[nodiscard]] bool loop_failed() const { return loop_failed_; }

	/// True if the loop and every session finished within \p limit.
	bool drain(std::chrono::milliseconds limit) {
		auto draining = std::async(std::launch::async, [this] {
			stdexec::sync_wait(
				stdexec::when_all(listener_.drained(), loop_scope_.join()));
		});
		drained_      = draining.wait_for(limit) == std::future_status::ready;
		return drained_;
	}

private:
	/// The listener on A: built as the server builds it, or on a
	/// FailingAcceptor that follows \p plan.
	static BasicListener<A> make_listener(asio::io_context &io,
										  std::shared_ptr<Services> services,
										  unsigned threads,
										  std::shared_ptr<FaultPlan> plan) {
		if constexpr (std::is_same_v<A, FailingAcceptor>)
			return BasicListener<A>(
				A(asio::make_strand(io.get_executor()), std::move(plan)),
				io.get_executor(),
				std::move(services),
				threads > 1);
		else
			return BasicListener<A>(io.get_executor(),
									std::move(services),
									threads > 1);
	}

	test::TempFile file_;
	std::shared_ptr<Services> services_;
	asio::io_context io_;
	asio::executor_work_guard<asio::io_context::executor_type> work_;
	BasicListener<A> listener_;
	/// Owns the accept loop, as the server's when_any does; after listener_,
	/// so it is destroyed first.
	stdexec::simple_counting_scope loop_scope_;
	std::atomic<bool> loop_failed_ = false;
	std::vector<std::thread> threads_;
	bool drained_ = false;
};

/// The listener exactly as the server runs it.
using ListenerFixture = BasicListenerFixture<tcp::acceptor>;
/// The listener with failed accepts injected.
using FailingListenerFixture = BasicListenerFixture<FailingAcceptor>;

} // namespace

SCENARIO("A listener accepts clients", "[net][listener]") {
	GIVEN("a listening server") {
		ListenerFixture fixture;

		WHEN("a client connects") {
			auto client = fixture.connect();

			THEN("it is served, repeatedly") {
				CHECK(ListenerFixture::ping(client));
				CHECK(ListenerFixture::ping(client));
			}
		}

		WHEN("many clients connect at once") {
			std::vector<tcp::socket> clients;
			clients.reserve(32);
			for (int i = 0; i < 32; ++i) clients.push_back(fixture.connect());

			THEN("every one is served") {
				for (auto &c : clients) CHECK(ListenerFixture::ping(c));
			}
		}
	}

	GIVEN("a server with several I/O threads") {
		ListenerFixture fixture(4);

		WHEN("many clients connect at once") {
			std::vector<tcp::socket> clients;
			clients.reserve(32);
			for (int i = 0; i < 32; ++i) clients.push_back(fixture.connect());

			THEN("every one is served, each on its own strand") {
				for (auto &c : clients) CHECK(ListenerFixture::ping(c));
			}
		}
	}
}

SCENARIO("A listener stops", "[net][listener][shutdown]") {
	GIVEN("a listening server with no clients") {
		ListenerFixture fixture;

		WHEN("it is stopped while waiting to accept") {
			fixture.stop();

			THEN("the accept loop ends without an error, and drains") {
				CHECK(fixture.drain(3s));
				CHECK(!fixture.loop_failed());
			}
		}
	}

	GIVEN("a server with a live client") {
		ListenerFixture fixture;
		auto client = fixture.connect();
		REQUIRE(ListenerFixture::ping(client));

		WHEN("it is stopped") {
			fixture.stop();

			THEN("the client is hung up on and everything drains") {
				CHECK(fixture.hung_up(client, 3s));
				CHECK(fixture.drain(3s));
				CHECK(!fixture.loop_failed());
			}
		}

		WHEN("it is stopped twice") {
			fixture.stop();
			fixture.stop();

			THEN("that is harmless") { CHECK(fixture.drain(3s)); }
		}
	}

	GIVEN("a stopped server") {
		ListenerFixture fixture;
		fixture.stop();
		REQUIRE(fixture.drain(3s));

		THEN("new connections are refused") { CHECK_THROWS(fixture.connect()); }
	}
}

SCENARIO("A listener survives failed accepts", "[net][listener][fault]") {
	GIVEN("a listener whose next three accepts fail") {
		const auto started = std::chrono::steady_clock::now();
		auto plan          = std::make_shared<FaultPlan>();
		plan->failures     = 3;
		FailingListenerFixture fixture(1, plan);

		WHEN("a client connects") {
			auto client = fixture.connect();

			THEN(
				"it is served once the failures are over, after a pause each") {
				CHECK(FailingListenerFixture::ping(client));
				CHECK(plan->injected == 3);
				// A lower bound only: each failure waits ACCEPT_RETRY_DELAY
				// before the next try, so the loop cannot have spun.
				CHECK(std::chrono::steady_clock::now() - started >=
					  3 * detail::ACCEPT_RETRY_DELAY);
				CHECK(!fixture.loop_failed());
			}
		}
	}

	GIVEN("a listener whose accepts keep failing") {
		auto plan      = std::make_shared<FaultPlan>();
		plan->failures = 1'000'000;
		FailingListenerFixture fixture(1, plan);

		WHEN("it has been failing for a while") {
			std::this_thread::sleep_for(350ms);

			THEN("it retried a few times, not in a tight loop") {
				// About one try per ACCEPT_RETRY_DELAY (100 ms): generous
				// bounds, so a slow machine does not fail the test.
				CHECK(plan->injected >= 1);
				CHECK(plan->injected <= 10);
			}
		}

		WHEN("it is stopped while waiting after a failure") {
			fixture.stop();

			THEN("the loop ends without an error, and drains") {
				CHECK(fixture.drain(3s));
				CHECK(!fixture.loop_failed());
			}
		}
	}
}
