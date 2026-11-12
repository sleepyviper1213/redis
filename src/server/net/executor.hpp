#pragma once
#include <exec/asio/asio_thread_pool.hpp>
#include <exec/variant_sender.hpp>
#include <stdexec/execution.hpp>

#include <concepts>
#include <optional>
#include <thread>
#include <utility>

namespace redis {

/**
 * \brief The single thread that owns the keyspace.
 *
 * With several I/O threads, sessions read, parse and write in parallel but
 * must still execute commands one at a time: the keyspace has no locks. A
 * session hands its parsed batch to run_on(), which runs it on this thread (a
 * stdexec thread pool of one) and continues back on the session's own I/O
 * executor once the batch has run. Commands from every session therefore
 * interleave on one thread, exactly as they do on a single-threaded server.
 *
 * Built unthreaded, run_on() calls the function inline instead and nothing is
 * handed off, so the single-threaded server pays nothing for this class.
 */
class Executor {
public:
	/// \param threaded Start the dedicated thread; otherwise run_on() is
	/// inline.
	explicit Executor(bool threaded);

	Executor(const Executor &)            = delete;
	Executor &operator=(const Executor &) = delete;

	~Executor();

	/**
	 * \brief Wait until everything already queued has run, so that nothing
	 * else touches the keyspace and the caller may. Only meaningful once no
	 * session can call run_on() any more, i.e. after the I/O threads stopped.
	 *
	 * \pre Must not be called from a task running on the keyspace thread itself
	 *      — that would deadlock waiting for a task only that thread can run.
	 *      In debug builds an assertion catches this.
	 */
	void stop();

	[[nodiscard]] bool is_threaded() const noexcept {
		return pool_.has_value();
	}

	/**
	 * \brief Call \p fn on the keyspace thread, then continue on \p back, a
	 * scheduler over the caller's own executor (see AsioContext). Completes
	 * with fn's result; an exception thrown by \p fn completes with set_error.
	 *
	 * \p fn may capture locals of the caller by reference, as long as the
	 * caller keeps them alive until the sender completes.
	 *
	 * Without a dedicated thread \p fn simply runs where the sender starts,
	 * and nothing is handed off.
	 *
	 * \tparam F The callable's type (std::invocable with no arguments): what
	 *         runs on the keyspace thread. Its return type is what the sender
	 *         completes with.
	 * \tparam Back The type of the scheduler to continue on (a
	 *         stdexec::scheduler), usually AsioContext's.
	 * \return A sender completing on \p back (or where it started, without a
	 *         dedicated thread). Its type is deduced: a variant_sender of the
	 *         handed-off and the inline chain, so both cases share one type.
	 */
	template <std::invocable F, stdexec::scheduler Back>
	stdexec::sender auto run_on(F fn, Back back) {
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
#ifndef NDEBUG
	/// The worker's id, for the assert in stop(). Unset when not threaded.
	std::thread::id pool_thread_;
#endif
};

} // namespace redis
