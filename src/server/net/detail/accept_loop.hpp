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

/**
 * \brief What accept_loop() needs of the listener it serves: when to stop, and
 * how to accept and to wait. All run on the acceptor's strand.
 *
 * An interface, not a set of callables: the three belong together, and one
 * vtable costs nothing to copy or to allocate. The listener outlives the loop.
 */
class AcceptSource {
public:
	/// Whether the acceptor is open: closing it is how the loop is told to end.
	[[nodiscard]] virtual bool is_open() const = 0;
	/// A fresh attempt: accept one client and start its session. Called afresh
	/// for every attempt, as that is what gives each client its own strand.
	[[nodiscard]] virtual AcceptAttempt attempt() = 0;
	/// Starts the wait after a failure; completes with is_open().
	[[nodiscard]] virtual AcceptPause pause() = 0;

protected:
	AcceptSource()                                = default;
	AcceptSource(const AcceptSource &)            = default;
	AcceptSource &operator=(const AcceptSource &) = default;
	~AcceptSource()                               = default; // not deleted through it
};

/**
 * \brief Accepts until the acceptor is closed or a stop request arrives.
 *
 * A failed attempt is logged (unless the acceptor was closed, which is how the
 * loop is told to end), then the loop waits as source.pause() says and tries
 * again, so a transient failure does not end it. Cancellation reaches the
 * suspended co_await as set_stopped and unwinds the task without an error.
 */
stdexec_ext::InlineTask<void> accept_loop(AcceptSource &source);

} // namespace redis::detail
