#pragma once

#include <chrono>
#include <concepts>

namespace redis {

/**
 * \brief A std::chrono clock: the standard Cpp17Clock requirements (ADR-0013).
 *
 * Spells out std::chrono::is_clock_v, which libc++ does not ship yet; once it
 * does, this becomes `std::chrono::is_clock_v<C>`.
 */
template <class C>
concept Clock = requires {
	typename C::rep;
	typename C::period;
	typename C::duration;
	typename C::time_point;
	{ C::is_steady } -> std::convertible_to<bool>;
	{ C::now() } -> std::same_as<typename C::time_point>;
};

/// A clock that never goes back: what an elapsed time must be measured on.
template <class C>
concept MonotonicClock = Clock<C> && C::is_steady;

} // namespace redis
