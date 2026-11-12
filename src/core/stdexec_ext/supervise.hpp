#pragma once

#include <stdexec/execution.hpp>

#include <cstdint>
#include <exception>
#include <type_traits>
#include <utility>

namespace redis::stdexec_ext {

/**
 * What the failure of one unit of work (a session, the accept loop, the
 * housekeeping timer) does to the rest of the server.
 */
enum class FailPolicy : std::uint8_t {
	/**
	 * The failure ends this unit only: it is handled where it happened, and
	 * the unit then completes normally. The sender can no longer fail, which
	 * is what stdexec::spawn requires. For work that comes and goes, such as
	 * one client's session.
	 */
	Isolate,
	/**
	 * The failure propagates as set_error. Run under exec::when_any, it
	 * cancels the siblings and the error reaches whoever started them. For
	 * work the server cannot do without.
	 */
	FailFast,
};

/**
 * \brief \p sender, with its failure handled according to \p Policy. A stop
 * request is not a failure and passes through either way.
 *
 * Isolate needs a handler, called once with the error; the sender then
 * completes normally. FailFast takes an optional observer, called once with
 * the error before it travels on unchanged: that is where the failure is
 * logged with what failed, since whoever receives the error only sees the
 * error. Neither may throw. Naming the policy at the call site is the point:
 * it is how a reader sees what a failure of that work does to everything
 * else.
 *
 * \tparam Policy What a failure does; see FailPolicy.
 * \tparam Sender The type of the supervised sender (a stdexec::sender).
 * \tparam Handler The type of the handler (Isolate: exactly one) or of the
 *         observer (FailFast: none or one): a callable invoked with each error
 *         type the sender can send, an exception_ptr usually, that does not
 *         throw.
 * \return A sender; its type is deduced. Under Isolate it sends no error;
 *         under FailFast it sends the same errors as \p sender.
 */
template <FailPolicy Policy, stdexec::sender Sender, class... Handler>
stdexec::sender auto supervised(Sender sender, Handler... on_error) {
	static_assert(Policy == FailPolicy::Isolate ? sizeof...(Handler) == 1
												: sizeof...(Handler) <= 1,
				  "supervised: Isolate takes one handler, FailFast at most one");
	static_assert(
		(std::is_nothrow_invocable_v<Handler, const std::exception_ptr &> &&
		 ...),
		"supervised: the handler must not throw");
	if constexpr (Policy == FailPolicy::Isolate) {
		return std::move(sender) |
			   stdexec::upon_error(std::move(on_error)...);
	} else if constexpr (sizeof...(Handler) == 0) {
		return std::move(sender);
	} else {
		return std::move(sender) |
			   stdexec::let_error(
				   [... observe = std::move(on_error)]<class Error>(
					   Error &&error) {
					   (observe(error), ...);
					   return stdexec::just_error(std::forward<Error>(error));
				   });
	}
}

} // namespace redis::stdexec_ext
