#pragma once

#include <stdexec/execution.hpp>

namespace redis::stdexec_ext {

/**
 * \brief The environment of an InlineTask: what makes it start and resume
 * inline.
 *
 * stdexec::task is scheduler-affine by default: it starts on the scheduler of
 * whoever starts it, and after every co_await hops back there. That costs a
 * scheduling operation per co_await, and stdexec::spawn has no scheduler to
 * offer in the first place. With inline_scheduler as the start scheduler
 * both go away: the task needs nothing from the environment, and since an
 * inline scheduler completes where it starts, the task resumes wherever the
 * awaited sender completed (for an Asio operation, on its executor) with no
 * hop. Errors and stop tokens keep stdexec::task's defaults.
 */
struct InlineTaskEnv {
	using start_scheduler_type = stdexec::inline_scheduler;
};

/**
 * \brief A coroutine that can be started as a sender, e.g. by stdexec::spawn,
 * and resumes inline (see InlineTaskEnv).
 *
 * A failure (an exception, or an awaited sender's error) completes the sender
 * with set_error(exception_ptr). A stop request reaching a suspended co_await
 * completes it with set_stopped, destroying the frame without resuming it.
 *
 * \tparam T What the coroutine co_returns.
 */
template <class T = void>
using InlineTask = stdexec::task<T, InlineTaskEnv>;

} // namespace redis::stdexec_ext
