#include "server/net/listener.hpp"

#include "core/stdexec_ext/supervise.hpp"
#include "server/net/detail/listener_impl.tpp" // instantiates the failing one
#include "server/services.hpp"
#include "support/loopback.hpp"
#include "support/temp_file.hpp"

#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/system/system_error.hpp>
#include <catch2/catch_test_macros.hpp>
#include <exec/variant_sender.hpp>
#include <stdexec/execution.hpp>

#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using namespace redis;
using namespace std::chrono_literals;
namespace asio = boost::asio;
using asio::ip::tcp;

namespace {

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
 * \brief A listener on an ephemeral loopback port, served by \p threads I/O
 * threads, with its accept loop running as the server runs it.
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
		  context_(threads),
		  listener_(make_listener(context_.io(), services_, threads,
								  std::move(plan))) {
		REQUIRE(listener_.listen("127.0.0.1", 0).has_value());
		// As the server does, but a failure is recorded, not fatal: the tests
		// check that stopping ends the loop without one.
		stdexec::spawn(
			stdexec_ext::supervised<stdexec_ext::FailPolicy::Isolate>(
				listener_.run(),
				[this](const std::exception_ptr &) noexcept {
					loop_failed_ = true;
				}),
			loop_scope_.get_token());
	}

	BasicListenerFixture(const BasicListenerFixture &)            = delete;
	BasicListenerFixture &operator=(const BasicListenerFixture &) = delete;

	~BasicListenerFixture() {
		listener_.stop();
		if (!drained_) drain_within(5s);
		// stop() posts the acceptor's close to an I/O thread. Without the
		// join, the listener (destroyed before context_) could be destroyed
		// while that close still runs: a race, found by TSan, and a use after
		// free. The server joins its I/O threads before its listener goes too.
		context_.join();
	}

	/// A client connected to the listener; throws if refused.
	test::Client connect() { return {context_.io(), listener_.port()}; }

	void stop() { listener_.stop(); }

	/// Whether the accept loop ended with an error rather than by stopping.
	[[nodiscard]] bool loop_failed() const { return loop_failed_; }

	/// Requires the accept loop and every session to finish within \p limit;
	/// see test::require_finishes_within.
	void drain_within(std::chrono::milliseconds limit) {
		test::require_finishes_within(
			stdexec::when_all(listener_.drained(), loop_scope_.join()),
			limit,
			"The listener");
		drained_ = true;
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
	test::RunningContext context_;
	BasicListener<A> listener_;
	/// Owns the accept loop, as the server's when_any does; after listener_,
	/// so it is destroyed first.
	stdexec::simple_counting_scope loop_scope_;
	std::atomic<bool> loop_failed_ = false;
	bool drained_                  = false;
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
				CHECK(client.ping_answered());
				CHECK(client.ping_answered());
			}
		}

		WHEN("many clients connect at once") {
			std::vector<test::Client> clients;
			clients.reserve(32);
			for (int i = 0; i < 32; ++i) clients.push_back(fixture.connect());

			THEN("every one is served") {
				for (auto &c : clients) CHECK(c.ping_answered());
			}
		}
	}

	GIVEN("a server with several I/O threads") {
		ListenerFixture fixture(4);

		WHEN("many clients connect at once") {
			std::vector<test::Client> clients;
			clients.reserve(32);
			for (int i = 0; i < 32; ++i) clients.push_back(fixture.connect());

			THEN("every one is served, each on its own strand") {
				for (auto &c : clients) CHECK(c.ping_answered());
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
				fixture.drain_within(3s);
				CHECK(!fixture.loop_failed());
			}
		}
	}

	GIVEN("a server with a live client") {
		ListenerFixture fixture;
		auto client = fixture.connect();
		REQUIRE(client.ping_answered());

		WHEN("it is stopped") {
			fixture.stop();

			THEN("the client is hung up on and everything drains") {
				CHECK(client.hangs_up_within(3s));
				fixture.drain_within(3s);
				CHECK(!fixture.loop_failed());
			}
		}

		WHEN("it is stopped twice") {
			fixture.stop();
			fixture.stop();

			THEN("that is harmless") { fixture.drain_within(3s); }
		}
	}

	GIVEN("a stopped server") {
		ListenerFixture fixture;
		fixture.stop();
		fixture.drain_within(3s);

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
				CHECK(client.ping_answered());
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
				fixture.drain_within(3s);
				CHECK(!fixture.loop_failed());
			}
		}
	}
}
