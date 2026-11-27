#pragma once

#include <concepts>
#include <type_traits>
#include <utility>

namespace redis {

/**
 * \brief Calls a function when the scope ends, on every exit path.
 *
 * From SerenityOS AK::ScopeGuard. Not copyable or movable: a copy would run
 * the function twice. The function runs in a destructor, so it must not
 * throw. Give it a named function or a named lambda, not a long inline one.
 */
template <typename F>
	requires std::invocable<F &> && std::is_nothrow_invocable_v<F &>
class [[nodiscard]] ScopeGuard {
public:
	explicit constexpr ScopeGuard(F function) noexcept(
		std::is_nothrow_move_constructible_v<F>)
		: function_(std::move(function)) {}

	ScopeGuard(const ScopeGuard &)            = delete;
	ScopeGuard &operator=(const ScopeGuard &) = delete;
	ScopeGuard(ScopeGuard &&)                 = delete;
	ScopeGuard &operator=(ScopeGuard &&)      = delete;

	constexpr ~ScopeGuard() { function_(); }

private:
	F function_;
};

} // namespace redis
