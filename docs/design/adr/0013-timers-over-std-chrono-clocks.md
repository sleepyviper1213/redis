# ADR-0013: Elapsed and deadline timers share one interface over std::chrono clocks

- **Status:** Accepted; amended by DX-93 (the simulated clocks are removed)
- **Date:** 2026-11-26
- **Requirements:** NFR-006, NFR-010.1, NFR-003
- **Issue / PR:** #54 (builds on #53, DX-90)

## Context

The code measures time in two ways:

- **Spans** (command duration, the time a background save has run, server uptime) are measured with
  `BasicElapsedTimer<Clock>`. Its template parameter is where precision and cost are chosen: `steady_clock` by default,
  `TickClock` on the hot path, where `steady_clock` costs a tenth of a pipelined `SET` on macOS. It plays the part
  `Qt::TimerType` plays for `QTimer`.
- **Deadlines** (key expiry) use `DeadlineTimer`, wired to the wall clock through `DateTime`. It has no clock
  parameter, a different shape, and there is no monotonic deadline.

Tests could replace the wall clock since #53, through the `WallClockSource` switch (removed again in DX-93), but nothing
could stand in for a monotonic clock. The watchdog's `HeartbeatSlot<T, Clock>` and the elapsed timer take a clock type, yet every test
passes the real `steady_clock`.

What the standard already provides, and where it falls short:

| Need | `std::chrono` | Gap |
|------|---------------|-----|
| Wall time as Unix time | `system_clock` (POSIX time, C++20 guarantees the Unix epoch) | `now()` is static: no test can replace it where the user is not a template |
| Monotonic time | `steady_clock` | Costly on macOS: `TickClock` |
| Cheap, millisecond-grade monotonic time | none | `CLOCK_MONOTONIC_COARSE` and kin, if ever measured to matter |
| Saturating arithmetic | none: `time_point` on a signed `rep` overflows, undefined behaviour | `DateTime`, and saturation in the deadline timer |
| Recognising a clock | `is_clock_v` (C++20) | not in libc++ yet |
| Simulated time | none | test clocks |

C++20's further clocks fill none of these gaps. `utc_clock` counts leap seconds, so its count is about 27 s ahead of
Unix time today: every `EXPIREAT` would be off by that much from Redis. `tai_clock` and `gps_clock` have no use in a
Redis server. `file_clock` is the clock of `std::filesystem::file_time_type`, and nothing here reads file times.
`high_resolution_clock` is an alias of one of the other two, and which one depends on the library.

## Decision

Both timers take a `std::chrono` clock as a template parameter, with the same members where they mean the same thing.
Standard clocks are the defaults; the code adds clocks only for the gaps above, each one a `std::chrono` clock on
standard time types.

```cpp
template <class C> concept Clock;           // the standard clock requirements, spelled out
template <class C> concept MonotonicClock;  // Clock<C> && C::is_steady

struct WallTime;      // time_point = sys_time<milliseconds>; now() reads DateTime::current()
struct TickClock;     // monotonic, cheap on macOS (exists)

template <MonotonicClock C = std::chrono::steady_clock> class BasicElapsedTimer;   // exists
template <Clock C = std::chrono::steady_clock>          class BasicDeadlineTimer;

using ElapsedTimer      = BasicElapsedTimer<>;
using TickTimer         = BasicElapsedTimer<TickClock>;
using DeadlineTimer     = BasicDeadlineTimer<WallTime>;   // key expiry
using TickDeadlineTimer = BasicDeadlineTimer<TickClock>;
```

- **What the template parameter chooses:** the clock's kind (wall or monotonic, precision, cost), checked by the type
  system: deadlines on different clocks have different `time_point` types and cannot be compared by mistake.
- **The wall clock is always the system clock (DX-93):** `WallTime::now()` reads `DateTime::current()`, which reads
  `system_clock`. The `WallClockSource` switch of DX-90 is removed, and with it every test seam in the server's clock.
- **Saturation:** `BasicDeadlineTimer::after()` saturates on `C::rep` for any clock; `forever()` is
  `time_point::max()`, which for `WallTime` is the same instant as `DateTime::distant_future()`.
- **Unix-time members** (`at_s`, `at_ms`, `after_s`, `after_ms`, `millis_since_epoch`, `date_time`) exist only for
  `WallTime`, through `requires`: an absolute Unix time means nothing on a monotonic clock.
- **Tests (DX-93):** no simulated clock. A test that needs a deadline already passed sets one in the past; a test that
  needs time to pass sleeps and is tagged `[timing]`, which the sanitizer test presets exclude.
- **Coarse clocks:** each would be one more clock type (`CoarseSteadyClock`: `CLOCK_MONOTONIC_COARSE`,
  `mach_approximate_time`, `GetTickCount64`), stating its resolution, added only when a user exists and a
  `redis-benchmark` profile shows clock reads matter. The wall clock gets no coarse variant: a key would outlive its
  deadline by a kernel tick and `PTTL` would be visibly off. If wall-clock reads ever matter, the answer is one reading
  per batch, as Redis does (`commandTimeSnapshot()`), under its own issue.

## Alternatives considered

- **An enum for the clock kind, as `Qt::TimerType`:** chosen per object at run time, it would let a `TickClock`
  deadline be compared with a wall-clock one, cost a branch per reading and keep shared state. `QTimer` needs an enum
  because it is a runtime object behind a non-template API; here the choice belongs in the type.
- **A template parameter for real or simulated wall time as well:** type-safe and without shared state, but `Database`,
  the commands, the snapshot and append-only-file code would all become templates for the sake of tests. Rejected in
  DX-90.
- **Keep `DeadlineTimer` separate, with matching member names only:** no call site changes, but two implementations
  of the same arithmetic, and still no monotonic deadline.
- **Drop `DateTime` for `sys_time<milliseconds>` everywhere:** less code, but every conversion would have to remember
  to saturate, and the snapshot and append-only-file code rely on `DateTime` doing it. Kept; `DeadlineTimer::date_time()`
  bridges the two exactly, since both are 64-bit milliseconds since the Unix epoch.
- **`utc_clock` for "correct" time:** wrong for Redis, whose clients and files exchange POSIX time.

## Consequences

- Elapsed and deadline timers read the same way, on any clock; a monotonic deadline (`TickDeadlineTimer`) is
  available, though nothing uses one yet. The gain today is one interface and testability, not removed code.
- Tests use real clocks only (DX-93): a test that waits is tagged `[timing]` and does not run under the sanitizers.
- `DeadlineTimer::deadline()` now returns `sys_time<milliseconds>`; code that stores a `DateTime` (the two expiry
  indexes) calls `date_time()`. `remaining_time_ms()` goes: on `WallTime`, `remaining_time()` is already milliseconds.
- When libc++ ships `std::chrono::is_clock_v`, the concepts become `is_clock_v<C>` and `is_clock_v<C> && C::is_steady`.
- Follow-ups, each its own issue when needed: a coarse monotonic clock; a per-batch cached "now"; a manual test
  scheduler so the `every` and watchdog loops can be stepped by hand.
