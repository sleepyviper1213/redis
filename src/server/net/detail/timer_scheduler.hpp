#pragma once
#include "asio_context.hpp"

#include <boost/asio/steady_timer.hpp>
#include <exec/asio/use_sender.hpp>
#include <exec/unless_stop_requested.hpp>
#include <stdexec/execution.hpp>

#include <chrono>

namespace redis::detail {

/**
 * \brief One Asio timer as a stdexec timed scheduler, so that the watchdog
 * vocabulary (time_limited() and the rest) can time work on an Asio executor.
 *
 * It reuses the timer rather than make one per wait: a session races every
 * round against its limits, and a timer per round would be one more object
 * to set up on the request path. The price is one wait at a time per timer,
 * which a round's race is.
 *
 * Waits complete on the timer's executor (a session's strand, say), and
 * schedule() runs on \p context, which should be over the same executor.
 * Both the timer and the context must outlive every sender scheduled here.
 */
class TimerScheduler {
public:
	TimerScheduler(AsioContext &context, boost::asio::steady_timer &timer)
		: context_(&context), timer_(&timer) {}

	[[nodiscard]] auto schedule() const {
		return stdexec::schedule(context_->get_scheduler());
	}

	/// The timer's clock: what exec::now() reads, and what the waits are
	/// timed on.
	[[nodiscard]] static std::chrono::steady_clock::time_point now() noexcept {
		return std::chrono::steady_clock::now();
	}

	/**
	 * Completes once \p after has passed since the sender started: the timer
	 * is armed then, not when this is called, so one sender restarted every
	 * round waits the whole time each round.
	 */
	[[nodiscard]] stdexec::sender auto
	schedule_after(std::chrono::steady_clock::duration after) const {
		return stdexec::just() | stdexec::let_value([timer = timer_, after] {
				   timer->expires_after(after);
				   // unless_stop_requested: in a race whose other side
				   // completes at once, the stop request comes before the
				   // wait starts. use_sender would lose it (its cancellation
				   // signal has no handler yet) and the race would wait out
				   // the whole time.
				   return exec::unless_stop_requested(
					   timer->async_wait(exec::asio::use_sender));
			   });
	}

	friend bool operator==(const TimerScheduler &,
						   const TimerScheduler &) noexcept = default;

private:
	AsioContext *context_;
	boost::asio::steady_timer *timer_;
};

} // namespace redis::detail
