#pragma once
/**
 * \file
 * \brief The watchdog vocabulary: senders that keep an eye on work which may
 * take too long, within structured concurrency.
 *
 * Every watch here is scoped: it starts with the work (or with the server,
 * for a monitor), ends before its sender completes, and leaves no timer
 * running behind it. They differ in what they do about slow work, and in how
 * they see it:
 *
 * | Watch          | Sees the work as        | Too long means        |
 * |----------------|-------------------------|-----------------------|
 * | watched()      | a sender, start to end  | a report; it runs on  |
 * | time_limited() | a sender, start to end  | it is cancelled       |
 * | escalate()     | a sender, start to end  | a report, then cancel |
 * | monitor()      | a HeartbeatSlot, polled | a report; it runs on  |
 *
 * Reports follow a ReportPolicy: once per piece of work, or again every
 * period while it lasts. The sender forms also come as adaptors, so
 * `work | time_limited(scheduler, 1s, on_expiry)` reads as the pipeline it
 * is.
 *
 * Which to use: time_limited() when the work can be cancelled and a late
 * answer is worthless; watched() when it cannot, or must finish anyway;
 * escalate() when slow work deserves a warning before it is given up on;
 * monitor() when there is no sender to wrap (synchronous work on a thread of
 * its own) or when a timer per piece of work would cost too much.
 *
 * Not to be confused with DeadlineTimer (core/chrono), a wall-clock time
 * point for key expiry: these limit work by a duration from its start.
 */

#include "watchdog/escalate.hpp"     // IWYU pragma: export
#include "watchdog/heartbeat.hpp"    // IWYU pragma: export
#include "watchdog/policy.hpp"       // IWYU pragma: export
#include "watchdog/time_limited.hpp" // IWYU pragma: export
#include "watchdog/watched.hpp"      // IWYU pragma: export
