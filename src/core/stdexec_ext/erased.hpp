#pragma once

#include <exec/any_sender_of.hpp>
#include <stdexec/execution.hpp>

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

} // namespace redis::stdexec_ext
