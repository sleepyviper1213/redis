# Key Expiration: Test Scenarios

Test scenarios for per-key expiry, derived from the acceptance criteria and requirements in the
[Key Expiry feature](README.md) and its [use cases](use_cases/) (FR-006.1 to FR-006.11, NFR-010). They
extend the [Master Test Plan](../../test/test_plan_and_report.md), whose section 12 lists FR-006 as covered at the unit level only.

Scenario IDs are `TC-nnn`, one sequence for the whole project ([ID ranges](../README.md#id-ranges)). Replies are quoted as the server writes them (RESP2 notation: `:1` integer, `$-1` null
bulk, `-ERR ...` error).

## How to read the coverage columns

Two columns, one question each:

| Column | Meaning |
|--------|---------|
| *Level* | The levels the scenario is designed for. A plan, not a record of what runs |
| *Tested at* | The levels where a test exists today, found from the *Existing test* column: `Unit`, `Integration`, `Stress`, `Differential`, or `none` |
| *Status* | `Covered` when every level in *Level* is in *Tested at*; `Partial` when some are; `Gap` when none are |

So a row with Level `Unit, Integration`, Tested at `Unit` and Status `Partial` has its unit test and lacks its
integration test. A `Differential` level counts as tested only once the differential run covers it and is gating (see
the note below the scenario tables).

## Levels and technique

| Level        | Use for                                                                                           |
|--------------|---------------------------------------------------------------------------------------------------|
| Unit         | Keyspace and command logic with no socket; time is controlled where the test can set deadlines      |
| Integration  | A running server through hiredis: real clock, real sweep, restart                                  |
| Differential | Replies compared with real Redis; only time-independent cases (no waiting, deadlines far ahead or already past) |

**Running the integration scenarios.** Those that need one server run with the rest of the suite
(`scripts/test_server.sh`, `test/integration/expiry.test.cpp`). Those that need a restart or the server's log are two
hidden phases, `[restart-write]` and `[restart-verify]` in `test/integration/expiry_restart.test.cpp`, run around a
server restart by `scripts/test_expiry_server.sh` (CI step "Key expiry restart test").

**Running the differential scenarios.** `scripts/diff_test.py --expiry` adds the expiry commands to the stream sent to this
server and to real Redis: `EXPIRE`, `PEXPIRE`, `EXPIREAT`, `PEXPIREAT`, `PERSIST`, `SET` with `EX`, `PX`, `EXAT`, `PXAT`,
`KEEPTTL`, wrong arity, and writes that keep or clear a deadline. Deadlines are far ahead, already past or invalid, never
short, and `TTL` and `PTTL` are compared as missing, no deadline, or "some time left", because the two servers cannot
agree on the remaining time to the millisecond. CI runs it as a gating step ("Differential test against Redis, expiry commands"): the first run with no divergence in 100000 requests was CI run 59. The scenarios marked Differential are
covered by it in that form; the marks below stay as they are until a run against Redis confirms them. The first run
(CI run 49) found: the overflow error text (#44, fixed), `EXPIRE k 10 11` answered `Unsupported option` by Redis (#41, FR-006.12),
and a `DBSIZE` difference after `SET` with an already-past deadline (#45: confirmed, see [the finding](finding_45_set_past_deadline.md); decision: follow Redis, bug #48, fixed). A later run (CI run 56) found a key lost by `SET` over a due key
(#49, fixed). Both servers count a due key in `DBSIZE` until it is read or swept, and which sweep comes first is timing, so the
generator reads the key at once (`EXISTS`) after a `SET` with a past deadline, and both servers then drop it the same
way; the unit test TC-263 covers the counting. The step gates the job since CI run 59, which showed no divergence. The Differential marks of the scenarios below have not been reviewed against that run.

**Timing rules for integration tests.** Use `PEXPIRE` with a small millisecond amount, then poll with a timeout instead
of one fixed sleep. A key is gone for commands the moment its deadline passes (TC-105); the sweep removes it from
memory within one 100 ms period (TC-107). Never assert `TTL` to the exact millisecond; assert a range.

**Timing rules for unit tests.** The same rule holds: `TTL` rounds to the nearest second, so a slow step (a sanitizer
build on a loaded runner) reads one second low. A test that sets a deadline of N seconds and reads `TTL` later asserts
N - 1 to N, through the `expect_ttl` helper in `expiry.test.cpp`. Only `-1` and `-2` are exact. CI run 64 on
`aarch64-apple-darwin-llvm-asan` read `99` for `100` in TC-266.

## 1. Setting a deadline (AC-01, 02, 03)

| ID        | Scenario                         | Given / When                                       | Then                                                      | Req            | Level                | Existing test                              | Tested at | Status |
|-----------|----------------------------------|----------------------------------------------------|-----------------------------------------------------------|----------------|----------------------|--------------------------------------------|----------|--------|
| TC-086 | `EXPIRE` on an existing key | Key `k` exists; `EXPIRE k 300` | `:1`; `TTL k` is in 299..300 | FR-006.1 | Unit, Differential | `keyspace`: "Setting and clearing expiry" | Unit | Partial |
| TC-087 | `PEXPIRE` in milliseconds | Key `k`; `PEXPIRE k 5000` | `:1`; `PTTL k` is in 4000..5000 | FR-006.1 | Unit, Differential | `keyspace`: "Setting and clearing expiry" | Unit | Partial |
| TC-088 | `EXPIREAT` in the future | Key `k`; `EXPIREAT k <now+100 s>` | `:1`; `TTL k` is about 100 | FR-006.1 | Unit, Differential | `keyspace`: "Setting and clearing expiry" | Unit | Partial |
| TC-089 | `PEXPIREAT` in the future | Key `k`; `PEXPIREAT k <now+100000 ms>` | `:1`; `PTTL k` is about 100000 | FR-006.1 | Unit, Differential | `keyspace`: "Setting and clearing expiry" | Unit | Partial |
| TC-090 | Every value type takes a deadline | Keys of string, list, hash and set; `EXPIRE` each | `:1` for each; each key keeps its value and type | FR-006.1 | Unit | `expiry`: "Every value type takes a deadline" | Unit | Covered |
| TC-091 | Missing key | No key `k`; `EXPIRE k 300` | `:0`; `EXISTS k` is `:0` | FR-006.1 | Unit, Differential | `keyspace`: "Setting and clearing expiry" | Unit | Partial |
| TC-092 | A new deadline replaces the old | `EXPIRE k 100`, then `EXPIRE k 500` | `TTL k` is about 500 | FR-006.1 | Unit | `expiry`: "Every value type takes a deadline" | Unit | Covered |
| TC-093 | `SET` with each expiry option | `SET k v EX 100`; `PX 100000`; `EXAT <s>`; `PXAT <ms>` | `+OK`; `GET k` is `v`; `TTL` matches the option | FR-006.2 | Unit, Integration | `set_ttl`: "SET applies each expiration option" | Unit | Partial |
| TC-094 | `SET` without `KEEPTTL` | `k` has a deadline; `SET k v2` | `TTL k` is `:-1` | FR-006.2, 006.7 | Unit, Differential | `set_ttl`: "SET handles an existing TTL like Redis" | Unit | Partial |
| TC-095 | `SET ... KEEPTTL` | `k` has a deadline; `SET k v2 KEEPTTL` | `GET k` is `v2`; deadline unchanged | FR-006.2, 006.7 | Unit, Differential | `set_ttl`: "SET handles an existing TTL like Redis" | Unit | Partial |
| TC-096 | `SET` expiry option conflicts | `SET k v EX 10 PX 10`; `SET k v EX 10 KEEPTTL`; `SET k v EX` | `-ERR syntax error`; `k` unchanged | FR-006.2, 010 | Unit, Differential | `set_ttl`: "SET option validation" | Unit | Partial |
| TC-097 | `SET` repeated option | `SET k v EX 10 EX 20` | `+OK`; the last expiry wins | FR-006.2 | Unit | `set_ttl`: "SET option validation" | Unit | Covered |

## 2. Deadlines already past and invalid amounts (AC-01, 02, 03)

| ID        | Scenario                         | Given / When                                       | Then                                                      | Req            | Level                | Existing test                              | Tested at | Status |
|-----------|----------------------------------|----------------------------------------------------|-----------------------------------------------------------|----------------|----------------------|--------------------------------------------|----------|--------|
| TC-098 | Zero or negative amount | Key `k`; `EXPIRE k 0`; again with `EXPIRE k -5` | `:1`; `EXISTS k` is `:0`; the log records `DEL` (TC-113) | FR-006.5 | Unit, Differential | `expiry`: "Deadlines already past, invalid amounts and wrong arity" | Unit | Partial |
| TC-099 | Absolute time in the past | Key `k`; `EXPIREAT k 1`; `PEXPIREAT k 1` | `:1`; the key is gone | FR-006.5 | Unit, Differential | `expiry`: "Deadlines already past, invalid amounts and wrong arity" | Unit | Partial |
| TC-263 | `SET` with a past `EXAT` or `PXAT` keeps the key until it is read | `DBSIZE` (n); `SET k v PXAT 1`; `DBSIZE`; `EXISTS k`; `DBSIZE` | `+OK`; `n + 1`; `:0`; `n` | FR-006.5, 006.8 | Unit, Differential | `expiry`: "SET with a past EXAT or PXAT keeps the key until it is read" | Unit | Partial |
| TC-264 | `DEL` of a due key | A key is due and not yet swept; `DEL k` | `:0`; `expired_keys` + 1 (#40, fixed) | FR-006.8, NFR-010.1 | Unit, Differential | `expiry`: "DEL of a key past its deadline replies 0, as Redis does" | Unit | Covered |
| TC-265 | `SET` over a key that is already due | `SET k a PXAT 1`; `SET k b PXAT 4102444800000`; `GET k`; then `SET k c PXAT 1`; `SET k d KEEPTTL`; `GET k`; `TTL k` | `$1 b`; then `d` and `:-1` (the due key is missing, so `KEEPTTL` keeps nothing); `expired_keys` + 2 (#49) | FR-006.5, 006.8 | Unit, Differential | `expiry`: "SET over a key that is already due" | Unit | Covered |
| TC-266 | Options decide whether a deadline is set | A key with a deadline 100 s ahead: `EXPIRE k 50 GT`; `EXPIRE k 200 GT`; `EXPIRE k 10 NX`; `EXPIRE k 10 XX`; `EXPIRE k 5 LT`; `EXPIRE k 9999 LT`. A key without a deadline: `XX`, `GT`, `LT`, `NX` | `:0`, `:1`, `:0`, `:1`, `:1`, `:0`; then `:0`, `:0`, `:1`, `:1`; `TTL` shows what was applied | FR-006.12 | Unit | `expiry`: "Options decide whether EXPIRE sets a deadline" | Unit | Covered |
| TC-267 | The other three commands take the options | `PEXPIRE`, `EXPIREAT`, `PEXPIREAT` with far-ahead deadlines and each option | The same rules; equal deadlines fail `GT` and `LT` | FR-006.12 | Unit, Differential | `expiry`: "The other EXPIRE commands take the options too" | Unit | Covered |
| TC-268 | Missing key, due key, past deadline | `EXPIRE nokey 10 NX`; a due key with `XX`; `EXPIRE k 0 LT` on a key with a deadline ahead | `:0`; `:0`; `:1` and the key is deleted | FR-006.12, 006.5 | Unit | `expiry`: "Options with a missing key, a due key and a past deadline" | Unit | Covered |
| TC-269 | Option errors | `EXPIRE k 10 11`; `EXPIRE k abc 11`; `EXPIRE k 10 NX XX`; `EXPIRE k 10 GT LT`; `EXPIRE k 10 11 NX XX`; `EXPIRE k abc NX`; `EXPIRE k 10 nx NX` | `-ERR Unsupported option 11` (also for the invalid amount, and before the incompatibility check); the two incompatibility errors; `-ERR value is not an integer or out of range`; `:1` | FR-006.12, 006.6 | Unit, Differential | `expiry`: "Refused options and the order of the checks" | Unit | Covered |
| TC-270 | Only an applied command is logged | `--appendonly`; key with a deadline; `EXPIRE k 50 GT` (not applied); `EXPIRE k 200 GT` (applied) | The log holds one `PEXPIREAT k <ms>`, for the applied one | FR-006.12, 006.10 | Unit | `aof`: "Only an EXPIRE that was applied is logged" | Unit | Covered |
| TC-100 | Not an integer | `EXPIRE k abc`; `EXPIRE k 1.5`; `PEXPIRE k ""` | `-ERR value is not an integer or out of range` | FR-006.6, 010 | Unit, Differential | `expiry`: "Deadlines already past, invalid amounts and wrong arity" | Unit | Partial |
| TC-101 | Deadline overflows | `EXPIRE k 9223372036854775807`; `PEXPIRE k 9223372036854775807` | `-ERR invalid expire time in 'expire' command` (`'pexpire'`, `'expireat'`; fixed in #44); the previous deadline is kept | FR-006.6 | Unit, Differential | `set_ttl`: "Expiry arguments that overflow the clock are rejected" | Unit | Partial |
| TC-102 | `SET` with a non-positive or overflowing expiry | `SET k v EX 0`; `EX -1`; `PX 9223372036854775807` | `-ERR invalid expire time in 'set' command`; `k` unchanged | FR-006.6 | Unit, Differential | `set_ttl`: "SET option validation", "Expiry arguments that overflow the clock are rejected" | Unit | Partial |
| TC-103 | Wrong arity | `EXPIRE k`; `TTL`; `PERSIST k x` (`EXPIRE k 1 2` is an unsupported option, TC-269) | `-ERR wrong number of arguments for '...' command` | FR-010 | Unit, Differential | `expiry`: "Deadlines already past, invalid amounts and wrong arity" | Unit | Partial |

## 3. Reading and cancelling (AC-04, 05, 06)

| ID        | Scenario                         | Given / When                                       | Then                                                      | Req            | Level                | Existing test                              | Tested at | Status |
|-----------|----------------------------------|----------------------------------------------------|-----------------------------------------------------------|----------------|----------------------|--------------------------------------------|----------|--------|
| TC-104 | `TTL` rounds, `PTTL` does not | `SET k v EX 100`, then `TTL k` at once | `:100` (not 99); `PTTL k` is in 99000..100000 | FR-006.3, NFR-010.4 | Unit, Differential | `set_ttl`: "SET applies each expiration option" | Unit | Partial |
| TC-104a | `TTL` and `PTTL` states | Missing key; key without deadline | `:-2`; `:-1` (both commands) | FR-006.3 | Unit, Differential | `keyspace`: "Setting and clearing expiry" | Unit | Partial |
| TC-104b | `PERSIST` | `k` has a deadline; `PERSIST k`; again; on a missing key | `:1`, then `:0`, then `:0`; `TTL k` is `:-1` | FR-006.4 | Unit, Differential | `keyspace`: "Setting and clearing expiry" | Unit | Partial |
| TC-104c | In-place writes keep the deadline | `k` has a deadline; `INCR`, `APPEND`, `LPUSH`, `HSET`, `SADD` on the matching types | `TTL` stays positive after each | FR-006.7 | Unit, Differential | `expiry`: "Writes keep or drop a deadline" | Unit | Partial |
| TC-104d | `MSET` clears the deadline | `k` has a deadline; `MSET k v` | `TTL k` is `:-1` | FR-006.7 | Unit, Differential | `expiry`: "Writes keep or drop a deadline" | Unit | Partial |
| TC-104e | `DEL` and `FLUSHDB` drop the deadline | `k` has a deadline; `DEL k`; `SET k v` | `TTL k` is `:-1`; same after `FLUSHDB` | FR-006.7 | Unit | `expiry`: "Writes keep or drop a deadline" | Unit | Covered |

## 4. Expiry on access (AC-07)

| ID        | Scenario                         | Given / When                                       | Then                                                      | Req            | Level                | Existing test                              | Tested at | Status |
|-----------|----------------------------------|----------------------------------------------------|-----------------------------------------------------------|----------------|----------------------|--------------------------------------------|----------|--------|
| TC-105 | Every read sees the key missing | `PEXPIRE k 50`; wait until 50 ms have passed; `GET`, `EXISTS`, `TYPE`, `TTL`, `LLEN`, `HGETALL` as the type allows | `$-1`/`:0`/`none`/`:-2`/empty, never the old value | FR-006.8, NFR-010.1 | Unit, Integration | `expiry`: "A key past its deadline is missing to every command"; integration `expiry`: "TC-105" | Integration, Unit | Covered |
| TC-106 | `KEYS` skips a due key | `PEXPIRE k 50`; wait; `KEYS *` | `k` is not listed | FR-006.8 | Unit | `expiry`: "A key past its deadline is missing to every command" | Unit | Covered |
| TC-106a | A write starts from empty | A list `k` is due; `LPUSH k x`; `LRANGE k 0 -1` | Only `x`; no `WRONGTYPE`; `TTL k` is `:-1` | FR-006.8 | Unit | `expiry`: "A key past its deadline is missing to every command" | Unit | Covered |
| TC-106b | A type change after expiry | A string `k` is due; `LPUSH k x` | Succeeds, not `WRONGTYPE` | FR-006.8 | Unit | `expiry`: "A key past its deadline is missing to every command" | Unit | Covered |
| TC-106c | `expired_keys` counts lazy removal | Note `expired_keys`; a due key is read; `INFO stats` | Counter rises by 1 | FR-006.8, 006.11 | Unit, Integration | `expiry`: "A key past its deadline is missing to every command" | Unit | Partial |

## 5. Active expiry (AC-08)

| ID        | Scenario                         | Given / When                                       | Then                                                      | Req            | Level                | Existing test                              | Tested at | Status |
|-----------|----------------------------------|----------------------------------------------------|-----------------------------------------------------------|----------------|----------------------|--------------------------------------------|----------|--------|
| TC-107 | Unread keys are swept | `PEXPIRE` 100 keys by 50 ms; read none; wait 300 ms | `DBSIZE` is `:0`; `expired_keys` rose by 100 | FR-006.9, NFR-010.2 | Integration | integration `expiry`: "TC-107"; `database`: "The active expiry cycle is bounded and takes the oldest first" (cycle only) | Integration, Unit | Covered |
| TC-108 | The sweep is bounded | 2500 keys all due; call the cycle with a limit of 1000 | Returns 1000; a second call 1000; a third 500; then 0 | FR-006.9, NFR-010.3 | Unit | `database`: "The active expiry cycle is bounded and takes the oldest first" | Unit | Covered |
| TC-109 | Oldest deadline first | Keys due at t1 < t2 < t3; limit 2 | The keys of t1 and t2 go; t3 stays | FR-006.9 | Unit | `database`: "The active expiry cycle is bounded and takes the oldest first" | Unit | Covered |
| TC-110 | The sweep does not touch live keys | Mix of due, future and persistent keys; one cycle | Only the due keys go; the others and their deadlines remain | FR-006.9 | Unit | `database`: "The active expiry cycle is bounded and takes the oldest first" | Unit | Covered |
| TC-110a | Commands and the sweep never overlap | Several clients run `SET ... PX` and `GET` while the sweep runs | Every reply matches a model; no sanitizer report | NFR-010.6, FR-009 | Stress, ThreadSanitizer | Stress test avoids expiry | none | Gap |

## 6. Persistence (AC-09)

| ID        | Scenario                         | Given / When                                       | Then                                                      | Req            | Level                | Existing test                              | Tested at | Status |
|-----------|----------------------------------|----------------------------------------------------|-----------------------------------------------------------|----------------|----------------------|--------------------------------------------|----------|--------|
| TC-111 | `EXPIRE` logged as absolute | `--appendonly`; `EXPIRE k 300` | The log holds `PEXPIREAT k <ms>`, not `EXPIRE` | FR-006.10 | Unit | `aof`: "Commands that cannot be replayed as received" | Unit | Covered |
| TC-112 | `SET ... EX` logged as `PXAT` | `--appendonly`; `SET k v EX 300` | The log holds `SET k v PXAT <ms>` without `NX`, `XX`, `GET` | FR-006.10 | Unit | `aof`: "Commands that cannot be replayed as received" | Unit | Covered |
| TC-113 | Past deadline logged | `--appendonly`; `EXPIRE k 0`; `PEXPIREAT k 1`; `SET k v PXAT 1` (`1` is positive, so it is accepted, and past); then `EXISTS k` | The log holds `DEL k` for the first two; `SET k v PXAT 1` for the third, then `DEL k` once `EXISTS` removes it | FR-006.10, 006.5 | Unit | `aof`: "A deadline already past is logged" | Unit | Covered |
| TC-114 | Expired key logged as `DEL` | `--appendonly`; key expires by access; another by the sweep | The log holds `DEL` for both, once each | FR-006.10 | Unit | `aof`: "A key that expires is logged as DEL, once" | Unit | Covered |
| TC-115 | Replay does not extend a lifetime | Log `EXPIRE k 300`; restart 100 s later (or replay with a fixed clock) | `TTL k` is about 200, not 300 | FR-006.10, NFR-010.5 | Unit, Integration | `aof`: "A replay does not extend a lifetime" | Unit | Partial |
| TC-116 | Nothing expires during replay | A log sets a key with a deadline already past by the time of replay, then reads it | The commands in the log see the key; expiry resumes after replay | FR-006.10 | Unit | `aof`: "A replay does not depend on when it runs" | Unit | Covered |
| TC-117 | Snapshot keeps deadlines | A key with a deadline; `SAVE`; restart | After load, `TTL k` matches the remaining time | FR-006.10, FR-011 | Unit, Integration | `snapshot`: "A snapshot keeps absolute deadlines"; integration `expiry_restart` via `scripts/test_expiry_server.sh` | Integration, Unit | Covered |
| TC-118 | Snapshot skips keys already due | A key with a short deadline; `SAVE`; restart after it passed | The key is not restored; `DBSIZE` excludes it | FR-006.10 | Unit, Integration | `snapshot`: "Snapshots round-trip every value type"; integration `expiry_restart` via `scripts/test_expiry_server.sh` | Integration, Unit | Covered |
| TC-119 | Base of the log keeps a deadline that passed since it was written | A base holds `k` with a deadline; the deadline passes before restart; a logged command uses `k` | On startup the command sees `k`; after the replay `k` expires and a `DEL` is logged | FR-006.10, FR-012 | Unit | `snapshot`: "A snapshot keeps absolute deadlines" (`keep_expired` load); `aof`: "A base keeps a deadline that passes before the restart" (a restart over the base and log files; fails if the base is loaded without `keep_expired`); the load itself in `snapshot` (#43) | Unit | Covered |

## 7. Observability and clock (AC-10)

| ID        | Scenario                         | Given / When                                       | Then                                                      | Req            | Level                | Existing test                              | Tested at | Status |
|-----------|----------------------------------|----------------------------------------------------|-----------------------------------------------------------|----------------|----------------------|--------------------------------------------|----------|--------|
| TC-120 | `INFO keyspace` counts deadlines | 3 keys, 2 with deadlines; `INFO keyspace` | `db0:keys=3,expires=2`; after `PERSIST` of one, `expires=1` | FR-006.11 | Unit, Integration | `expiry`: "INFO reports the keys with a deadline and the keys that expired" | Unit | Partial |
| TC-121 | `INFO stats` counts expiry | 5 keys expire (access or sweep); `INFO stats` | `expired_keys` is 5; `FLUSHDB` does not add to it | FR-006.11 | Unit, Integration | `expiry`: "INFO reports the keys with a deadline and the keys that expired" | Unit | Partial |
| TC-122 | Trace log of the sweep | `--log-level trace`; keys expire by the sweep | `Expired N keys` is logged; no key name appears | FR-006.9, 3.2.3 | Integration | `scripts/test_expiry_server.sh` (trace log of the sweep, no key name) with `expiry_restart` "[restart-write]" | Integration | Covered |
| TC-123 | Deadline arithmetic saturates safely | `DeadlineTimer` near the largest time | No overflow; the commands reject instead (TC-101) | NFR-010.1 | Unit | `deadline_timer` | Unit | Covered |
| TC-322 | A test can fix the wall clock ([#53](https://github.com/sleepyviper1213/redis/issues/53), DX-90) | A `test::SimulatedWallClock` at a chosen time; `advance(1500ms)`; the clock destroyed | `DateTime::current()` is the chosen time, then 1.5 s later, then the system clock again (between two system readings) | NFR-003 | Unit | `date_time`: "A test can fix the wall clock" | Unit | Covered |
| TC-323 | A deadline passes when the clock reaches it, not before (DX-90) | Simulated clock; `SET k v PX 50`; advance 49 ms, then 1 ms more; the machine as slow as it likes | `TTL`/`PTTL` count down from the simulated clock; the key is there at 49 ms and gone at 50 ms | FR-006.2, FR-006.8, NFR-010.1 | Unit | `set_ttl`: "SET applies each expiration option" (PX, 49 ms then the last millisecond) | Unit | Covered |
| TC-324 | No expiry or persistence unit test depends on the real clock (DX-90) | The `database`, `expiry`, `keyspace`, `set_ttl`, `aof` and `snapshot` unit tests | None sleeps to make a deadline pass or reads `system_clock` itself; each makes time pass with the simulated clock. Sleeping while a forked child finishes (`BGSAVE`, `BGREWRITEAOF`) waits on a process, not a deadline, and stays. `aarch64-apple-darwin-llvm-asan` passes twice in a row | NFR-003 | Unit | All six converted; the 213 unit tests pass on Windows (MinGW). Before: run 45771728216 failed four of them, on both attempts | Unit | Partial: the two macOS ASan passes in a row are pending |
| TC-325 | Both timers read a simulated clock exactly ([#54](https://github.com/sleepyviper1213/redis/issues/54), ADR-0013) | `BasicElapsedTimer` and `BasicDeadlineTimer` on `test::SimulatedClock<>`; a deadline 100 ms ahead; advance 40 ms, then 60 ms | Elapsed reads 40 ms, then 100 ms; `restart()` returns 100 ms; the deadline has 60 ms left, then 0, and has expired exactly at 100 ms | NFR-006, NFR-010.1 | Unit | `deadline_timer`: "TC-325: both timers read a simulated clock exactly" | Unit | Covered |
| TC-326 | A deadline saturates on any clock (ADR-0013) | `after()` with the largest duration, on `SimulatedClock`, `steady_clock` and `WallTime`; `forever()` | `after()` lands on `forever()` without overflow (UBSan quiet); `forever()` never expires, its remaining time is the largest duration | NFR-010.1 | Unit | `deadline_timer`: "TC-326: a deadline saturates on any clock" (also on `TickClock`, and the most negative duration); UBSan in CI | Unit | Covered |
| TC-327 | `DeadlineTimer` keeps its behaviour on `WallTime` (ADR-0013) | `at_ms`, `at_s`, `after_ms`, `millis_since_epoch`, `date_time()`; with a `SimulatedWallClock` | Unix times round-trip; `date_time()` equals the `DateTime` of the same instant; `after_ms(50)` expires when the simulated wall clock advances 50 ms; the existing `deadline_timer` tests pass unchanged in meaning | NFR-010.1, FR-006.1 | Unit | `deadline_timer`: "TC-327: DeadlineTimer keeps Unix times on the wall clock", and TC-123 unchanged; compile-time checks that only `DeadlineTimer` has the Unix-time members | Unit | Covered |
| TC-328 | `SimulatedClock` is a monotonic clock that only moves when told (ADR-0013) | Two tags; advance one; `reset()`; a negative advance | It meets `MonotonicClock`; the other tag does not move; `reset()` returns it to zero; a negative advance fails a `REDIS_VERIFY` (not tested: it aborts), so `now()` never goes back | NFR-003 | Unit | `deadline_timer`: "TC-328: a simulated clock moves only when told", and `static_assert`s on every clock's concept | Unit | Covered |

## Bugs, unimplemented features and limits by design

Three kinds of behaviour that differ from what a reader might expect, each handled differently. The tests are in
`test/unit/server/commands/expiry.test.cpp`.

| Kind | Meaning | ID | Test |
|------|---------|----|------|
| Bug | The server breaks a requirement or differs from Redis without intent | Issue number | The **correct** expectation, tagged `[!shouldfail]`; when the bug is fixed the test starts to pass and the tag is removed |
| Not implemented | A feature or option Redis has and the server lacks | Issue number | Pins today's reply; replaced by normal scenarios when the feature is added |
| By design | A deliberate limitation, the same as Redis or documented as accepted | `LI-nn` | Pins today's behaviour |

| ID    | Kind            | Behaviour                                             | Given / When                              | Today's result                                        | Test |
|-------|-----------------|-------------------------------------------------------|-------------------------------------------|-------------------------------------------------------|------|
| #42   | Not implemented | No `EXPIRETIME`/`PEXPIRETIME`                         | `EXPIRETIME k`                            | `-ERR unknown command 'EXPIRETIME'` (as the server words it) | same |
| LI-05 | By design       | `DBSIZE` counts a due key not yet swept (as Redis)    | A key is due; sweep not yet run; `DBSIZE` | The key is counted                                    | same |

## Coverage summary

Criteria to scenarios to coverage is kept in [traceability.md](traceability.md), so there is one table to update.

## Entry and exit criteria

* **Entry**: the server builds; the unit and integration suites run in the chosen preset.
* **Exit**: every scenario is Unit or higher, or has a Gap recorded in the test plan's anomaly list (section 16); the
  scenarios marked ThreadSanitizer pass under the `tsan` preset.
