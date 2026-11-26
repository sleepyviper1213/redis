# Key Expiry: Design Choices

Choices specific to this feature. System-wide design stays in the
[SDD](../../design/software_design_document.md); a decision that was a real trade-off has an
[ADR](../../design/adr/). Rows are added when the feature changes.

| #       | Choice | Why | Where recorded |
|---------|--------|-----|----------------|
| DX-17 | Two indexes: a map of key to deadline, and an ordered set of (deadline, key) | O(1) `TTL`, `PTTL`, `PERSIST`; the sweep takes the oldest due keys in O(log n) each | [ADR-0005](adr/0005-two-expiry-indexes.md), SDD §6.2 |
| DX-18 | Lazy removal on access, plus an active sweep | A key nobody reads still leaves memory; no command ever sees a due key | SDD §13.1, FR-006.8, FR-006.9 |
| DX-19 | Sweep every 100 ms, at most 1000 keys, oldest first | A tick stays short so clients are not held up; a key may stay in memory a little past its deadline | SDD §13.1, NFR-010.2, NFR-010.3 |
| DX-20 | Sweep and commands on the one keyspace thread | They never overlap, so no locks and no race between expiry and a command | [ADR-0001](../connection/adr/0001-single-threaded-command-executor.md), NFR-010.6 |
| DX-21 | Deadlines are absolute wall-clock times | `EXPIREAT`, `PXAT` and snapshots name calendar times; the cost is clock sensitivity (see known limits in the [README](README.md)) | FR-006.10 |
| DX-22 | Snapshots and the append-only file store absolute deadlines; expired keys are logged as `DEL`; replay runs with expiry off | A restart or a replay can never extend a lifetime | [ADR-0006](../persistence/adr/0006-own-snapshot-format.md), FR-006.10, NFR-010.5 |
| DX-23 | A deadline already past, set by the `EXPIRE` family, deletes the key at once; `SET` with a past `EXAT` or `PXAT` keeps the key with that deadline until it is read or swept | Matches Redis 8.0.2, which deletes in `expireGenericCommand` and not in `setGenericCommand` | FR-006.5, [finding](finding_45_set_past_deadline.md), #48 |
| DX-79 | The `EXPIRE` family parses its options before the amount, and only an applied command is logged; a key without a deadline counts as infinite for `GT` and `LT` | Matches Redis 8.0.2 (`parseExtendedExpireArgumentsOrReply`, `expireGenericCommand`); a replay then repeats only what changed the keyspace | FR-006.12, #41 |
| DX-90 | `DateTime::current()`, the one reading of the wall clock, takes its time from the source that `enum class WallClockSource` names, after `Qt::TimerType`: `System` (`std::chrono::system_clock`; the server, always) or `Fixed`, a time that moves only when a test says so (`DateTime::use_source()`, through the scoped `test::SimulatedWallClock`). Expiry and persistence unit tests make time pass with it, never by sleeping. The enum says only real or simulated; a clock's kind (precision, cost) is a template parameter | A test that sleeps past a deadline measures the machine, not the code: on a loaded runner the next command ran after the deadline (#53, anomaly A-1). An enum states the source in use, where an optional time with a sentinel value would hide it. It is a runtime switch, not a template parameter, because `Database` and the commands hold a plain `DeadlineTimer`. The cost is one acquire load per reading, a plain load on x86 and ARM64; threading a clock through `Database` and the commands would touch every signature for the same result | #53, TC-322 to TC-324, `src/core/chrono/date_time.cpp` |

## Code map

Per the [master test plan §12](../../test/test_plan_and_report.md): `server/commands/set_ttl`,
`core/chrono/deadline_timer`, `common/store/database` (`basic_database.hpp`), and the persistence code in
`server/persistence` (`set_expiry_enabled` switches expiry off during a replay).
Check this list against the source when the design changes.

## Open questions

* Support `EXPIRETIME` and `PEXPIRETIME`? Not required by the SRS today; would add requirements and change #42.
