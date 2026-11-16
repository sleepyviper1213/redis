#pragma once
/**
 * \file
 * The accept loop of a BasicListener, without the acceptor's type.
 *
 * The loop is a retry() over senders, whose deduced completion signatures are
 * costly to compute (each is a deep tree of let_value, then and repeat_until).
 * Written inside BasicListener<A>::run() it would be built again for every
 * acceptor type, and its types would depend on that acceptor's sender. Here it
 * is built once, in accept_loop.cpp, over erased senders: an accept costs one
 * allocation, and it happens once per client.
 */

#include "core/stdexec_ext/erased.hpp"
#include "core/stdexec_ext/task.hpp"

#include <stdexec/execution.hpp>

#include <chrono>
#include <exception>
#include <functional>

namespace redis::detail {

/// How long to wait after a failed accept. The failure usually persists (out of
/// descriptors lasts until one is freed), and retrying at once would spin.
inline constexpr auto ACCEPT_RETRY_DELAY = std::chrono::milliseconds(100);

/// One accept, serving the client it got: completes with nothing.
using AcceptAttempt = stdexec_ext::ErasedSender<stdexec::set_value_t(),
								   stdexec::set_error_t(std::exception_ptr),
								   stdexec::set_stopped_t()>;

/// The wait after a failed accept: completes with whether to try again.
using AcceptPause = stdexec_ext::ErasedSender<stdexec::set_value_t(bool),
								 stdexec::set_error_t(std::exception_ptr),
								 stdexec::set_stopped_t()>;

/// What accept_loop() needs of the listener it serves. All run on the
/// acceptor's strand.
struct AcceptHooks {
	/// Called once, as the loop starts.
	std::function<void()> announce;
	/// Whether the acceptor is open: closing it is how the loop is told to end.
	std::function<bool()> is_open;
	/// A fresh attempt: accept one client and start its session. Called afresh
	/// for every attempt, as that is what gives each client its own strand.
	std::function<AcceptAttempt()> attempt;
	/// Starts the wait after a failure; completes with is_open().
	std::function<AcceptPause()> pause;
};

/**
 * \brief Accepts until the acceptor is closed or a stop request arrives.
 *
 * A failed attempt is logged (unless the acceptor was closed, which is how the
 * loop is told to end), then the loop waits as hooks.pause says and tries
 * again, so a transient failure does not end it. Cancellation reaches the
 * suspended co_await as set_stopped and unwinds the task without an error.
 */
stdexec_ext::InlineTask<void> accept_loop(AcceptHooks hooks);

} // namespace redis::detail
