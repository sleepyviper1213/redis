#include "redis_server.hpp"

#include "core/error/failed.hpp"
#include "core/stdexec_ext/supervise.hpp"
#include "log_channels.hpp"
#include "net/detail/asio_context.hpp"
#include "net/formatter.hpp"
#include "net/listener.hpp"
#include "net/net_error.hpp"
#include "services.hpp"

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/steady_timer.hpp>
#include <exec/asio/use_sender.hpp>
#include <exec/start_detached.hpp>
#include <exec/when_any.hpp>
#include <fmt/format.h>
#include <stdexec/execution.hpp>

#include <algorithm>
#include <csignal>
#include <exception>
#include <expected>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace redis {
namespace {
spdlog::logger &log() { return logging::logger_for(logging::channel::server); }

// What an exception caught on an Asio thread says. Asio hands failures over as
// exception_ptrs, so the only way to read one is to throw it again.
std::string describe(const std::exception_ptr &error) {
	try {
		std::rethrow_exception(error);
	} catch (const std::exception &e) { return e.what(); } catch (...) {
		return "unknown exception";
	}
}

std::string_view signal_name(int sig) {
	switch (sig) {
	case SIGINT: return "SIGINT";
	case SIGTERM: return "SIGTERM";
	default: return "unknown signal";
	}
}

/**
 * The first failure of the server, whichever task or I/O thread it came from:
 * what run() returns. record() may be called from any thread; message() only
 * once every thread that could record has been joined.
 */
class FirstFailure {
public:
	/// Keeps \p message if nothing was recorded before; drops it otherwise.
	void record(std::string message) {
		std::call_once(once_, [&] { message_ = std::move(message); });
	}

	/// What was recorded, if anything.
	[[nodiscard]] std::optional<std::string> &message() { return message_; }

private:
	std::once_flag once_;
	std::optional<std::string> message_;
};

/**
 * \brief Serve until the first of the accept loop, the housekeeping and the
 * wait for a signal finishes; then cancel the others, drain the sessions and
 * stop the event loop.
 *
 * A coroutine, not a sender chain: it runs once, and it is sequential logic
 * whose failure path is naturally a try/catch. The pieces it strings
 * together stay senders.
 *
 * The first of the three to finish, however it finishes, cancels the other
 * two through the stop token, and when_any completes once they have unwound,
 * with the first one's result. The accept loop and the housekeeping are
 * FailFast: the server is of no use without them, so their failure ends it,
 * and is recorded in \p failure. A signal is the normal way to end.
 *
 * \p stop applies to the race only, not to the whole coroutine: a stop request
 * does not resume a coroutine, it destroys its frame. Were the coroutine
 * itself stoppable, a stop arriving while it waits for the sessions to drain
 * would skip io.stop() below, and the server would never exit.
 *
 * Every reference must outlive the coroutine; see run().
 *
 * \param housekeeping RedisServer::housekeeping(), not yet started.
 * \param stop Ends the race from outside: an I/O thread that failed.
 */
// The references are the point: these objects are run()'s, which outlives
// the coroutine by joining every I/O thread first (see the lifetime note
// there), and copies of a listener or an io_context would not be the same
// objects.
// NOLINTBEGIN(cppcoreguidelines-avoid-reference-coroutine-parameters)
stdexec_ext::InlineTask<void>
serve(Listener &server, stdexec_ext::InlineTask<void> housekeeping,
	  boost::asio::signal_set &signals, boost::asio::io_context &io,
	  stdexec::inplace_stop_token stop, FirstFailure &failure) {
	// NOLINTEND(cppcoreguidelines-avoid-reference-coroutine-parameters)
	using stdexec_ext::FailPolicy;
	using stdexec_ext::supervised;

	stdexec::sender auto signalled =
		signals.async_wait(exec::asio::use_sender) | stdexec::then([](int sig) {
			log().info("Received {}, shutting down", signal_name(sig));
		});

	try {
		co_await (
			exec::when_any(
				supervised<FailPolicy::FailFast>(
					server.run(),
					[](const std::exception_ptr &error) noexcept {
						log_error({.kind   = ConnectionKind::AcceptLoopFailed,
								   .quoted = describe(error)});
					}),
				supervised<FailPolicy::FailFast>(std::move(housekeeping)),
				signalled) |
			stdexec::write_env(stdexec::prop{stdexec::get_stop_token, stop})
			// Stopped from outside is still an end: carry on to the shutdown
			// below rather than have the frame destroyed.
			| stdexec::upon_stopped([]() noexcept {}));
	} catch (...) {
		// co_await turns whatever error ended the race into an exception.
		failure.record(describe(std::current_exception()));
	}

	// Sessions are not in the race, since they come and go: cancel them now,
	// and stop the loop only once they have unwound. Their scope must not be
	// destroyed while they run.
	server.stop();
	co_await server.drained();
	io.stop();
}
} // namespace

RedisServer::RedisServer(ServerConfig config)
	: config_(std::move(config)),
	  io_(static_cast<int>(std::max(1U, config_.io_threads))),
	  signals_(io_, SIGINT, SIGTERM),
	  command_watchdog_(diagnostics::ReportPolicy::Once, config_.watchdog),
	  services_(std::make_shared<Services>(
		  config_.dump_file, config_.io_threads > 1, config_.aof,
		  config_.limits, config_.config_set_enabled)) {}

std::expected<void, ServerError> RedisServer::run() {
	// With an append-only file it is the source of truth: it holds everything
	// since the last rewrite, which the snapshot does not.
	const auto loaded =
		services_->aof
			? services_->aof->load(services_->ctx, services_->handler)
			: services_->persistence.load(services_->db);
	if (!loaded) {
		// Refuse to start rather than serve an empty keyspace and later
		// overwrite the unreadable files with it.
		REDIS_FAILED(ServerError{.kind   = ServerKind::LoadFailed,
								 .source = loaded.error()});
	}

	Listener server(io_.get_executor(), services_, config_.io_threads > 1);
	if (auto listening = server.listen(config_.host, config_.port); !listening)
		REDIS_FAILED(ServerError{.kind   = ServerKind::ListenFailed,
								 .source = listening.error()});

	FirstFailure failure;
	// Stops serve()'s tasks from outside them: an I/O thread that failed.
	stdexec::inplace_stop_source stop_serving;

	// Lifetime: serve() holds references to server, failure and stop_serving,
	// locals of this function, and its steps run on the I/O threads, not here
	// (InlineTask has no scheduler affinity). That is safe only because this
	// function destroys them after every I/O thread has been joined below, and
	// the last thing serve() does is stop the loop those threads run.
	exec::start_detached(serve(server,
							   housekeeping(),
							   signals_,
							   io_,
							   stop_serving.get_token(),
							   failure));

	// io_.run() returns as soon as io_ has no work left, which is not the same
	// as serve() having finished: at shutdown serve() waits for the watchdog,
	// whose cancelled wait completes on the watchdog's own thread and is no
	// work of io_'s. Returning then would destroy what serve() still uses (see
	// the lifetime note above). So the loop runs until serve() stops it, which
	// is the last thing serve() does.
	const auto until_served = boost::asio::make_work_guard(io_);

	log().info("Ready to accept connections");
	{
		// Asio handlers can throw; this is where that ends. A failed I/O
		// thread stops the others, as it would a single-threaded io_.run().
		const auto run_io = [&] {
			try {
				io_.run();
			} catch (const std::exception &e) {
				failure.record(e.what());
				stop_serving.request_stop();
			} catch (...) {
				failure.record("unknown exception");
				stop_serving.request_stop();
			}
		};
		std::vector<std::jthread> workers;
		for (unsigned i = 1; i < config_.io_threads; ++i)
			workers.emplace_back(run_io);
		run_io();
	} // joins the workers: from here on, nothing else touches failure
	if (auto &message = failure.message())
		REDIS_FAILED(ServerError{.kind   = ServerKind::WorkerFailed,
								 .detail = std::move(*message)});

	// A batch may still be executing: wait for it before reading the keyspace.
	services_->keyspace.stop();

	// A running BGSAVE would make the final save below fail ("already in
	// progress"), and its snapshot predates the latest writes anyway.
	services_->persistence.cancel_background_save();

	// Nothing runs commands any more: what is logged is final. An unfinished
	// rewrite is dropped; the files it would have replaced are still whole.
	if (services_->aof) {
		services_->aof->cancel_rewrite();
		services_->aof->close();
	}

	// Redis refuses to exit when this fails, and keeps serving. This server
	// cannot resume once its loop has stopped, so it fails loudly instead: the
	// error is returned and main() exits non-zero, which a supervisor sees,
	// rather than a clean shutdown that lost the data.
	if (auto saved = services_->persistence.save(services_->db); !saved)
		REDIS_FAILED(ServerError{.kind   = ServerKind::FinalSaveFailed,
								 .source = saved.error()});
	log().info("Redis is now ready to exit, bye bye...");
	return {};
}

stdexec_ext::InlineTask<void> RedisServer::housekeeping() {
	// Neither ends on its own, so when_all ends when the race in serve() stops
	// it, or when one fails, which stops the other. A watchdog with no
	// threshold has nothing to do and completes at once, leaving cron().
	// Started with the race, after the load in run(), so a long AOF replay is
	// not reported as a stall; nor is the final save, which comes after.
	//
	// The watchdog completes on its own thread: a stop request from another
	// thread is handed to that thread, which completes the wait it cancels.
	// when_all completes where its last child does, and so would serve(),
	// which carries on with the shutdown (stopping the listener, draining the
	// sessions) that must run on the event loop. Hence the hop back to it, as
	// cron() hops back from the keyspace thread. The event loop still runs
	// then: serve() stops it only after this has completed.
	detail::AsioContext back(io_.get_executor());
	co_await stdexec::when_all(cron(),
							   command_watchdog_.run() |
								   stdexec::continues_on(back.get_scheduler()));
}

stdexec_ext::InlineTask<void> RedisServer::cron() {
	boost::asio::steady_timer timer(io_);
	// Housekeeping touches the keyspace on its thread and comes back here.
	detail::AsioContext back(io_.get_executor());
	const auto services = services_;
	while (true) {
		timer.expires_after(CRON_PERIOD);
		co_await timer.async_wait(exec::asio::use_sender);

		// Touches the keyspace, so it runs where commands run.
		co_await services->keyspace.run_on(
			[&services] {
				if (const auto expired =
						services->db.active_expire_cycle(EXPIRE_KEYS_PER_RUN))
					log().trace("Expired {} keys", expired);
				services->stats.sample();
				services->persistence.poll();
				if (services->aof) {
					// The expiry above, and the writes since the last batch,
					// if any, need to reach the file too.
					services->aof->log_expired(services->db);
					services->aof->after_batch();
					services->aof->poll();
				}
			},
			back.get_scheduler());
	}
}

} // namespace redis
