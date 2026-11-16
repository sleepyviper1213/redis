#pragma once

#include <exec/any_sender_of.hpp>
#include <stdexec/execution.hpp>

#include <concepts>
#include <type_traits>
#include <utility>

namespace redis::stdexec_ext {

/**
 * \brief A type-erased sender completing with \p Signatures.
 *
 * Erasing ends a deduced sender type where it is: whatever is built on it
 * sees one small type, not the tree of senders inside. stdexec computes a
 * sender's completion signatures many times over, each time through the
 * whole tree, so a long chain costs compile time and memory out of proportion
 * to its length; an erased stage in the middle cuts that. The price is an
 * allocation per stage started (its operation state is on the heap), so it
 * belongs where that is rare: once per client or per accept, not per byte.
 *
 * The stop token is forwarded, so cancellation reaches what is inside. It is
 * built from a sender that ends in a stdexec stage (then, let_value, ...), not
 * from an Asio operation directly: Asio's receiver cannot wrap an erased one.
 *
 * \tparam Signatures The completion signatures, e.g. set_value_t(int).
 */
template <class... Signatures>
using ErasedSender = exec::any_sender<
	exec::any_receiver<stdexec::completion_signatures<Signatures...>,
					   exec::queries<stdexec::inplace_stop_token(
						   stdexec::get_stop_token_t) noexcept>>>;

/**
 * \brief A sender that, each time it starts, builds \p Func's sender and runs
 * it erased as an ErasedSender<Signatures...>.
 *
 * An erased sender can be connected once and not copied, while a loop such as
 * repeat_until copies its child every round. This one is copyable (it holds
 * only \p func) and builds a new erased sender per start, which is also what
 * makes the cost per round, not per loop.
 *
 * The inner sender runs behind a let_value of its own: stdexec cannot build
 * some senders (when_any, for one) against the erased receiver's environment,
 * which, a polymorphic proxy, cannot be forwarded by value, and the
 * let_value's environment can.
 *
 * \tparam Signatures The completion signatures of the erased sender; the
 *         inner sender's must be among them.
 * \tparam Func Called with no arguments, const, returning a sender; copyable.
 * \return A sender; its type is deduced.
 */
template <class... Signatures, class Func>
	requires std::invocable<const Func &> &&
			 stdexec::sender<std::invoke_result_t<const Func &>>
stdexec::sender auto erased_per_start(Func func) {
	return stdexec::just() |
		   stdexec::let_value(
			   [func = std::move(func)]() -> ErasedSender<Signatures...> {
				   return stdexec::just() |
						  stdexec::let_value([&func] { return func(); });
			   });
}

} // namespace redis::stdexec_ext
