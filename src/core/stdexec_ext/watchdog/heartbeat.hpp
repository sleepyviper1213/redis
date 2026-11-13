#pragma once

#include "core/stdexec_ext/every.hpp"
#include "core/utils/attributes.hpp"
#include "policy.hpp"

#include <stdexec/execution.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <type_traits>
#include <utility>

namespace redis::stdexec_ext {

/**
 * \brief What a heartbeat carries: plain bytes, copied in and out whole.
 * \tparam T The type of the value.
 */
template <class T>
concept HeartbeatValue =
	std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>;

/**
 * \brief One read of a HeartbeatSlot: the work running then, and since when.
 * \tparam T The work's description, a HeartbeatValue.
 * \tparam Clock The clock it was stamped with.
 */
template <HeartbeatValue T, class Clock>
struct Beat {
	/// Different for every beat, and growing: two reads of one long piece of
	/// work can be told from two pieces of work that each look long.
	std::uint64_t generation = 0;
	typename Clock::time_point started;
	T subject;
};

/**
 * \brief Where work that cannot be a sender says what it is doing, for a
 * monitor on another thread to read: the probing member of the watchdog
 * vocabulary (see watchdog.hpp).
 *
 * watched() and time_limited() watch a sender from its start to its completion.
 * Synchronous work (a command running inside a batch, a loop with no
 * suspension point) has no sender to wrap, and wrapping each piece would
 * cost a timer per piece. Instead it beats as each piece starts and rests
 * when it ends, which costs a few stores, and monitor() looks every period.
 *
 * One writer; any number of readers on any thread. A sequence lock: a read
 * that a beat overlaps is thrown away (read() gives nullopt) rather than mix
 * two beats, so a monitor never sees one piece of work's description with
 * another's start. Every field is atomic, the value word by word, so reading
 * while the writer writes is no data race.
 *
 * Constant-initialized, so a slot may be a global that a crash handler or
 * another global's constructor reads safely.
 *
 * \tparam T The work's description, a HeartbeatValue. Keep it small: it is
 *         copied a word at a time on every beat.
 * \tparam Clock A clock with an integral representation, so that the start
 *         fits an atomic.
 */
template <HeartbeatValue T, class Clock = std::chrono::steady_clock>
	requires std::integral<typename Clock::rep>
class HeartbeatSlot {
public:
	using time_point = typename Clock::time_point;

	constexpr HeartbeatSlot() noexcept = default;

	HeartbeatSlot(const HeartbeatSlot &)            = delete;
	HeartbeatSlot &operator=(const HeartbeatSlot &) = delete;
	HeartbeatSlot(HeartbeatSlot &&)                 = delete;
	HeartbeatSlot &operator=(HeartbeatSlot &&)      = delete;
	~HeartbeatSlot()                                = default;

	/// The work \p subject started at \p started. Writer only.
	void beat(const T &subject, time_point started) noexcept {
		write(true, subject, started.time_since_epoch().count());
	}

	/// No work is running. Writer only.
	void rest() noexcept { write(false, T{}, 0); }

	/// The work running now, or nullopt when none is, or when a beat
	/// overlapped the read (that work has only just started). Any thread.
	[[nodiscard]] std::optional<Beat<T, Clock>> read() const noexcept {
		const auto sequence = sequence_.load(std::memory_order_acquire);
		if (sequence % 2 != 0) return std::nullopt; // being written
		const bool busy    = busy_.load(std::memory_order_relaxed);
		const auto started = started_.load(std::memory_order_relaxed);
		Words words{};
		for (std::size_t i = 0; i < WORDS; ++i)
			words[i] = subject_[i].load(std::memory_order_relaxed);
		// The loads above must not be seen after the one below.
		std::atomic_thread_fence(std::memory_order_acquire);
		if (sequence_.load(std::memory_order_relaxed) != sequence)
			return std::nullopt; // a beat overlapped: the fields may mix
		if (!busy) return std::nullopt;

		Beat<T, Clock> beat{.generation = sequence / 2,
							.started =
								time_point{typename Clock::duration{started}},
							.subject = {}};
		std::memcpy(&beat.subject, words.data(), sizeof(T));
		return beat;
	}

private:
	using Word = std::uintptr_t;
	static constexpr std::size_t WORDS =
		(sizeof(T) + sizeof(Word) - 1) / sizeof(Word);
	using Words = std::array<Word, WORDS>;

	void write(bool busy, const T &subject,
			   typename Clock::rep started) noexcept {
		Words words{};
		std::memcpy(words.data(), &subject, sizeof(T));
		// One writer, so plain stores do: no read-modify-write needed.
		const auto sequence = sequence_.load(std::memory_order_relaxed);
		sequence_.store(sequence + 1, std::memory_order_relaxed); // odd
		// The stores below must not be seen before the one above.
		std::atomic_thread_fence(std::memory_order_release);
		busy_.store(busy, std::memory_order_relaxed);
		started_.store(started, std::memory_order_relaxed);
		for (std::size_t i = 0; i < WORDS; ++i)
			subject_[i].store(words[i], std::memory_order_relaxed);
		sequence_.store(sequence + 2, std::memory_order_release); // even
	}

	/// Odd while a beat is being written; two more for every one.
	std::atomic<std::uint64_t> sequence_{0};
	std::atomic<bool> busy_{false};
	std::atomic<typename Clock::rep> started_{0};
	std::array<std::atomic<Word>, WORDS> subject_{};
};

/**
 * \brief Work that has been running longer than the threshold.
 * \tparam T, Clock As the HeartbeatSlot's.
 */
template <HeartbeatValue T, class Clock>
struct Stall {
	T subject;
	typename Clock::duration running;
};

/**
 * \brief Decides, from a HeartbeatSlot, whether the running work has
 * stalled, and whether to report it (again) under its ReportPolicy.
 *
 * What monitor() judges with, apart so that it can be tested without a
 * thread or real time: check() takes the time to judge by. Used by one thread
 * at a time.
 */
template <HeartbeatValue T, class Clock>
class StallDetector {
public:
	/// Judges \p slot, which must outlive the detector.
	StallDetector(const HeartbeatSlot<T, Clock> &slot REDIS_LIFETIMEBOUND,
				  ReportPolicy policy,
				  typename Clock::duration threshold) noexcept
		: slot_(&slot), policy_(policy), threshold_(threshold) {}

	/// The running work, if it has run the threshold by \p now and the policy
	/// lets it be reported (again).
	[[nodiscard]] std::optional<Stall<T, Clock>>
	check(typename Clock::time_point now) noexcept {
		const auto beat = slot_->read();
		if (!beat) return std::nullopt;
		if (policy_ == ReportPolicy::Once && beat->generation == reported_)
			return std::nullopt;
		const auto running = now - beat->started;
		if (running < threshold_) return std::nullopt;
		reported_ = beat->generation;
		return Stall<T, Clock>{.subject = beat->subject, .running = running};
	}

private:
	const HeartbeatSlot<T, Clock> *slot_;
	ReportPolicy policy_;
	typename Clock::duration threshold_;
	/// The generation last reported. Generations start at 1.
	std::uint64_t reported_ = 0;
};

namespace detail {
/// What one monitor() run keeps between looks, in let_value's operation
/// state: every() copies its step each round, the detector must remember.
template <HeartbeatValue T, class Clock, class StallFunc>
struct MonitorState {
	StallDetector<T, Clock> detector;
	StallFunc on_stall;

	/// One look, at \p Clock's time: the scheduler's clock times the waits
	/// only, not the work, which was stamped with \p Clock.
	void look() noexcept {
		if (const auto stall = detector.check(Clock::now())) on_stall(*stall);
	}
};
} // namespace detail

/**
 * \brief Look at \p slot every \p period, on \p scheduler, and tell
 * \p on_stall about work that has run \p threshold, under \p policy, until
 * stopped.
 *
 * The heartbeat's counterpart of watched(): the work is never interrupted,
 * only reported, and a report comes up to \p period after the threshold.
 * The policy is a value here, not a template argument as for watched(): it
 * changes no type.
 *
 * Like every(), it ends only when stopped, so it fits beside the server's
 * other long-running tasks in a when_any or when_all, and completes as
 * stopped on \p scheduler's thread.
 *
 * \tparam Scheduler A TimedScheduler with a thread of its own: not the one
 *         the work runs on, or the look would wait behind what it should
 *         report.
 * \tparam StallFunc What is told about each stall, on \p scheduler's thread;
 *         it must not throw.
 * \return A sender; its type is deduced.
 */
template <HeartbeatValue T, class Clock, class Scheduler, class Period,
		  class StallFunc>
	requires TimedScheduler<Scheduler, Period> &&
			 std::is_nothrow_invocable_v<StallFunc &, const Stall<T, Clock> &>
stdexec::sender auto
monitor(const HeartbeatSlot<T, Clock> &slot REDIS_LIFETIMEBOUND,
		Scheduler scheduler, Period period, ReportPolicy policy,
		typename Clock::duration threshold, StallFunc on_stall) {
	using State = detail::MonitorState<T, Clock, StallFunc>;
	return stdexec::just(State{
			   .detector = StallDetector<T, Clock>(slot, policy, threshold),
			   .on_stall = std::move(on_stall)}) |
		   stdexec::let_value(
			   [scheduler = std::move(scheduler), period](State &state) {
				   return every(scheduler, period, [&state]() noexcept {
					   state.look();
				   });
			   });
}

} // namespace redis::stdexec_ext
