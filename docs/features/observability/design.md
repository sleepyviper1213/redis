# Observability: Design Choices

Choices specific to this feature. System-wide design stays in the
[SDD](../../design/software_design_document.md); a decision that was a real trade-off has an
[ADR](../../design/adr/). Rows are added when the feature changes.

| #        | Choice | Why | Where recorded |
|----------|--------|-----|----------------|
| DX-45 | `INFO` builds `# Section` blocks of `field:value` lines; no argument or `default` gives the default sections, `all` adds `commandstats`, an unknown section gives an empty reply | Field names and shape as in Redis so existing tools parse them | UC-014, NFR-004, FR-007, `src/server/commands/server.cpp` |
| DX-46 | Counters live in `ServerStats`, written only by the command thread with a plain load and store; any thread may read the totals | No atomic read-modify-write on the hot path (from code) | SDD §14.2, `src/server/stats.hpp` |
| DX-47 | The dispatcher counts calls and time per command around every command | `INFO commandstats` finds slow commands under the watchdog threshold | UC-018 Notes, SDD §11.1 step 4 |
| DX-48 | Memory in `INFO` is counted by a counting allocator in the keyspace | `used_memory` reports real bytes | SDD §5.1, §14.4 |
| DX-49 | `CONFIG` supports only `GET <pattern>` and `SET loglevel`; `SET` is off unless `--enable-config-set` | Any client that can reach the port could change settings, and there is no authentication | UC-014, UC-015, SDD §17, FR-007 |
| DX-50 | Four log channels (`server`, `net`, `commands`, `persistence`), each a clone of the default logger, so one level and one file apply to all | A line says which part produced it; one setting configures everything | SDD §5.1, FR-015, `src/server/log_channels.hpp`, `src/core/logging/channels.hpp` |
| DX-51 | The logger is asynchronous with a bounded queue that overruns the oldest line; lines at warning and above are flushed at once | Logging must not block the server; warnings must survive a crash (from code) | SDD §14.1, UC-018 Notes, `src/core/logging/lifecycle.cpp` |
| DX-52 | Logging is scoped: a guard in `main` initialises it and ends it on every return path | Joins the async worker on an ordinary thread, not during static destruction (from code comment) | SDD §9 (RAII), `src/core/logging/guard.hpp` |
| DX-53 | `CONFIG SET loglevel` sets every channel and logs the change at warning level | The change is on record at any level short of error | UC-015 |
| DX-54 | The dispatcher records the running command (name, argument count, start); the watchdog thread reads it every 100 ms and warns while it runs past `--watchdog-ms`; `0` turns it off | One slow command stalls every client (ADR-0001); the warning is written while it runs, so a command that never ends is reported too | [ADR-0001](../connection/adr/0001-single-threaded-command-executor.md), SDD §14.1, §17, FR-016, UC-018 A1 |
| DX-55 | The crash report is written with `write(2)` to stderr and the log file, not through the logger; then the signal's default action runs | An async logger's queue dies with the process; core dumps stay available | SDD §10.2, FR-016, `src/server/diagnostics/crash.hpp` |
| DX-81 | The first fatal signal arms `alarm(3)`; the `SIGALRM` handler resets `SIGABRT` to its default and calls `abort()` | A report that hangs (a lock held by the crashed thread, a corrupt heap behind a libc call) must not leave a dead process alive; Abseil's failure handler does the same. Test seam: `REDIS_TEST_CRASH_REPORT_HANG` (read at install) stalls the report so TC-301 can reach the timeout | DX-55, `src/server/diagnostics/crash_posix.cpp` |
| DX-84 | The crash report says `Stack overflow` when a `SIGSEGV` or `SIGBUS` fault address lies within one guard region below the stack base of the faulting thread. Bounds are read once at install time (`pthread_getattr_np` on Linux; `pthread_get_stackaddr_np` and `pthread_get_stacksize_np` on macOS) and kept in plain integers | An overflow looks like any other fault, so the report said only `SIGSEGV`; the bounds come from SerenityOS `AK::StackInfo`. Only threads that have an alternate stack can report one (see Open questions) | DX-55, `src/server/diagnostics/crash_posix.cpp`, TC-302, TC-306 |
| DX-85 | Proposed: the stack trace is gathered by a frame-pointer walker (`unwind_frames`) that takes the memory read and the per-frame action as parameters, checks each frame against the stack bounds and stops on a null return address, a frame pointer that does not rise, or a depth of 64; `backtrace()` stays as the fallback where frame pointers are absent | `backtrace()` is not async-signal-safe (the unwinder can lock or allocate) and can hang on a corrupt stack, which only the DX-81 alarm then ends. A walk is plain reads. The parameters let it run in a unit test on any platform. Idea from SerenityOS `AK::StackUnwinder`. Not decided: needs `-fno-omit-frame-pointer`, so the benchmark cost is measured first | DX-81, TC-303 to TC-305 |
| DX-80 | The keyspace allocator is `JemallocAllocator<T>` when `REDIS_USE_JEMALLOC` is on and the platform has jemalloc, else `CountingAllocator`; it counts usable size; only the `DefaultAllocator` alias changes. No traits parameter beside the allocator | Less fragmentation under churn, as Redis does; the database is already generic over its allocator, and nothing else varies | [ADR-0011](adr/0011-jemalloc-keyspace-allocator.md), #50, `src/common/store/allocator.hpp` |

## Code map

* `src/server/commands/server.cpp`: `INFO`, `CONFIG`; `src/server/stats.hpp`: `ServerStats`, `CommandStat`.
* `src/core/logging/channels.cpp`, `src/core/logging/lifecycle.cpp`, `src/core/logging/guard.cpp`, `src/core/logging/settings.hpp`: the logging mechanism.
* `src/core/counting_allocator.hpp`, `src/common/store/allocator.hpp`: the keyspace allocator and the `DefaultAllocator` alias `src/core/jemalloc_allocator.hpp` (DX-80, built with `REDIS_USE_JEMALLOC`).
* `src/server/log_channels.hpp`: the channel list.
* `src/server/diagnostics/command_watchdog.cpp`, `src/server/diagnostics/current_command.cpp`, `src/server/diagnostics/crash.hpp`, `src/server/diagnostics/crash_posix.cpp`: watchdog, running-command record, crash report.
* `src/core/stdexec_ext/watchdog/`: the generic stall detection the command watchdog uses.
* `src/server/net/guard.hpp`: connection counters reported by `INFO`.

## Open questions

* DX-85: adopt the frame-pointer walker or keep `backtrace()`? Decide after `redis-benchmark` runs with and without `-fno-omit-frame-pointer`; the walker gives addresses only, resolved offline with `addr2line`.
* `sigaltstack` is per thread, and only the thread that calls `install_crash_handler` has one. A stack overflow on another thread (I/O, watchdog) kills the process with no report (TC-306). Fix: give each thread an alternate stack when it starts, or accept the limit and document it.
* `MEMORY DOCTOR` and a slow-command log (like `SLOWLOG`): see [TODO.md](../../../TODO.md) (Slow commands and resource limits).
* SDD §17: the watchdog reports a slow command, it does not prevent it; on Windows there is no crash handler.
* No authentication (FR-020), so `--enable-config-set` is safe only on a private port (SDD §17).
