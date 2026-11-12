#pragma once

#include <exec/task.hpp>

namespace redis::stdexec_ext {

/**
 * \brief A coroutine that can be started as a sender, e.g. by stdexec::spawn.
 *
 * exec::task's default context pins it to the scheduler of whoever awaits it,
 * so it is only awaitable from another coroutine, and spawn() has no such
 * scheduler. The inline context drops that affinity: the coroutine resumes
 * wherever the sender it awaited completed (for an Asio operation, on its
 * executor), and with it the requirement.
 *
 * stdexec::task (P3552) would be the standard choice, but in the pinned
 * stdexec it is awaitable only, not yet a sender. The one reference to
 * exec::__task, an internal namespace, is here: revisit on upgrade.
 *
 * A failure (an exception, or an awaited sender's error) completes the sender
 * with set_error(exception_ptr); a stop request reaching a suspended co_await
 * completes it with set_stopped, unwinding the coroutine without an error.
 */
template <class T = void>
using InlineTask = exec::basic_task<T, exec::__task::inline_task_context<T>>;

} // namespace redis::stdexec_ext
