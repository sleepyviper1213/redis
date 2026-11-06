#include "elapsed_timer.hpp"

namespace redis {

ElapsedTimer::ElapsedTimer() noexcept : start_(clock::now()) {}

void ElapsedTimer::start() noexcept { start_ = clock::now(); }

[[nodiscard]] std::chrono::milliseconds ElapsedTimer::restart() noexcept {
	const auto ms = elapsed();
	start_        = clock::now();
	return ms;
}

[[nodiscard]] ElapsedTimer::Duration
ElapsedTimer::duration_elapsed() const noexcept {
	return clock::now() - start_;
}

[[nodiscard]] std::chrono::milliseconds
ElapsedTimer::elapsed() const noexcept {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		duration_elapsed());
}

[[nodiscard]] int64_t ElapsedTimer::nsecs_elapsed() const noexcept {
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
			   duration_elapsed())
		.count();
}

[[nodiscard]] bool
ElapsedTimer::has_expired(std::chrono::milliseconds timeout) const noexcept {
	return elapsed() >= timeout;
}

} // namespace redis
