#pragma once
// Must precede every Asio include: makes co_await on Asio operations return
// (error_code, result) tuples.
#include "nothrow_awaitable_t.hpp" // IWYU pragma: keep

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <exec/asio/asio_thread_pool.hpp>
#include <exec/variant_sender.hpp>
#include <stdexec/execution.hpp>

#include <concepts>
#include <optional>
#include <type_traits>
#include <utility>

namespace redis {
namespace net = boost::asio;

/**
 * \brief The single thread that owns the keyspace, reachable from Asio
 * coroutines.
 *
 * With several I/O threads, sessions read, parse and write in parallel but
 * must still execute commands one at a time: the keyspace has no locks. A
 * session hands its parsed batch to run(), which runs it on this thread (a
 * stdexec thread pool of one) and resumes the coroutine back on its own I/O
 * executor once the batch has run; v2 sessions use run_on() to the same end. Commands from every session therefore interleave on one
 * thread, exactly as they do on a single-threaded server.
 *
 * Built unthreaded, run() calls the function inline instead and nothing is
 * handed off, so the single-threaded server pays nothing for this class.
 */
class Executor {
public:
	/// \param threaded Start the dedicated thread; otherwise run() is inline.
	explicit Executor(bool threaded);

	Executor(const Executor &)            = delete;
	Executor &operator=(const Executor &) = delete;

	~Executor();

	/**
	 * \brief Wait until everything already queued has run, so that nothing
	 * else touches the keyspace and the caller may. Only meaningful once no
	 * coroutine can call run() any more, i.e. after the I/O threads stopped.
	 */
	void stop();

	[[nodiscard]] bool is_threaded() const noexcept {
		return pool_.has_value();
	}

	/**
	 * \brief Call \p fn on the keyspace thread and resume the awaiting
	 * coroutine on its own executor with the result.
	 *
	 * \p fn may capture locals of the awaiting coroutine by reference: the
	 * coroutine is suspended until \p fn has returned. An exception thrown by
	 * \p fn is rethrown from the co_await.
	 */
	template <std::invocable F>
	net::awaitable<std::invoke_result_t<F>> run(F fn) {
		using Result = std::invoke_result_t<F>;
		if (!is_threaded()) co_return fn();

		// co_spawn onto the pool's own Asio executor runs the lambda there and
		// completes on this coroutine's executor, so the coroutine is never
		// resumed on the keyspace thread. The pool is stdexec's; only its Asio
		// face is used here.
		co_return co_await net::co_spawn(
			pool_->get_executor(),
			[&fn]() -> net::awaitable<Result> { co_return fn(); },
			net::use_awaitable);
	}

	/**
	 * \brief The sender form of run(), for v2 sessions: call \p fn on the
	 * keyspace thread, then continue on \p back, a scheduler over the
	 * caller's own executor (see AsioContext). Completes with fn's result;
	 * an exception thrown by \p fn completes with set_error.
	 *
	 * Without a dedicated thread \p fn simply runs where the sender starts,
	 * and nothing is handed off.
	 */
	template <std::invocable F, stdexec::scheduler Back>
	auto run_on(F fn, Back back) {
		using namespace stdexec;
		using Hop    = decltype(schedule(pool_->get_scheduler()) | then(fn) |
								continues_on(back));
		using Local  = decltype(just() | then(fn));
		using Sender = exec::variant_sender<Hop, Local>;

		if (!pool_) return Sender(just() | then(std::move(fn)));
		return Sender(schedule(pool_->get_scheduler()) | then(std::move(fn)) |
					  continues_on(std::move(back)));
	}

private:
	/// One thread: tasks run one at a time, in the order they were queued.
	std::optional<exec::asio::asio_thread_pool> pool_;
};

} // namespace redis
