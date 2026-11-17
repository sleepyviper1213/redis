# Key Expiration: Test Scenarios

Test scenarios for per-key expiry, derived from the acceptance criteria and requirements in the
[Key Expiry feature](README.md) and its [use cases](use_cases/) (FR-006.1 to FR-006.11, NFR-010). They
extend the [Master Test Plan](../../test/test_plan_and_report.md), whose section 12 lists FR-006 as covered at the unit level only.

Scenario IDs are `TC-nnn`, one sequence for the whole project ([ID ranges](../README.md#id-ranges)). Replies are quoted as the server writes them (RESP2 notation: `:1` integer, `$-1` null
bulk, `-ERR ...` error).

## How to read the coverage column

| Mark    | Meaning                                                                                          |
|---------|--------------------------------------------------------------------------------------------------|
| Unit    | A unit test whose scenario name matches; named in the *Existing test* column                     |
| Gap     | No test found; to be written at the level in the *Level* column                                  |
| Partial | A test covers part of the scenario                                                               |

Coverage was matched by test and scenario names in `test/unit/`, not by reading every assertion; confirm when a
scenario is written or run.

## Levels and technique

| Level        | Use for                                                                                           |
|--------------|---------------------------------------------------------------------------------------------------|
| Unit         | Keyspace and command logic with no socket; time is controlled where the test can set deadlines      |
| Integration  | A running server through hiredis: real clock, real sweep, restart                                  |
| Differential | Replies compared with real Redis; only time-independent cases (no waiting, deadlines far ahead or already past) |

**Timing rules for integration tests.** Use `PEXPIRE` with a small millisecond amount, then poll with a timeout instead
of one fixed sleep. A key is gone for commands the moment its deadline passes (TC-105); the sweep removes it from
memory within one 100 ms period (TC-107). Never assert `TTL` to the exact millisecond; assert a range.

## 1. Setting a deadline (AC-01, 02, 03)

| ID        | Scenario                         | Given / When                                       | Then                                                      | Req            | Level                | Existing test                              | Coverage |
|-----------|----------------------------------|----------------------------------------------------|-----------------------------------------------------------|----------------|----------------------|--------------------------------------------|----------|
| TC-086 | `EXPIRE` on an existing key      | Key `k` exists; `EXPIRE k 300`                     | `:1`; `TTL k` is in 299..300                              | FR-006.1       | Unit, Differential   | `keyspace`: "Setting and clearing expiry"  | Unit     |
| TC-087 | `PEXPIRE` in milliseconds        | Key `k`; `PEXPIRE k 5000`                          | `:1`; `PTTL k` is in 4000..5000                           | FR-006.1       | Unit, Differential   | `keyspace`: "Setting and clearing expiry"  | Partial  |
| TC-088 | `EXPIREAT` in the future         | Key `k`; `EXPIREAT k <now+100 s>`                  | `:1`; `TTL k` is about 100                                | FR-006.1       | Unit, Differential   | `keyspace`: "Setting and clearing expiry"  | Partial  |
| TC-089 | `PEXPIREAT` in the future        | Key `k`; `PEXPIREAT k <now+100000 ms>`             | `:1`; `PTTL k` is about 100000                            | FR-006.1       | Unit, Differential   | `keyspace`: "Setting and clearing expiry"  | Partial  |
| TC-090 | Every value type takes a deadline | Keys of string, list, hash and set; `EXPIRE` each  | `:1` for each; each key keeps its value and type          | FR-006.1       | Unit                 | `expiry`: "Every value type takes a deadline" | Unit |
| TC-091 | Missing key                      | No key `k`; `EXPIRE k 300`                         | `:0`; `EXISTS k` is `:0`                                  | FR-006.1       | Unit, Differential   | `keyspace`: "Setting and clearing expiry"  | Partial  |
| TC-092 | A new deadline replaces the old  | `EXPIRE k 100`, then `EXPIRE k 500`                | `TTL k` is about 500                                      | FR-006.1       | Unit                 | `expiry`: "Every value type takes a deadline" | Unit |
| TC-093 | `SET` with each expiry option    | `SET k v EX 100`; `PX 100000`; `EXAT <s>`; `PXAT <ms>` | `+OK`; `GET k` is `v`; `TTL` matches the option       | FR-006.2       | Unit, Integration    | `set_ttl`: "SET applies each expiration option" | Unit |
| TC-094 | `SET` without `KEEPTTL`          | `k` has a deadline; `SET k v2`                     | `TTL k` is `:-1`                                          | FR-006.2, 006.7 | Unit, Differential  | `set_ttl`: "SET handles an existing TTL like Redis" | Unit |
| TC-095 | `SET ... KEEPTTL`                | `k` has a deadline; `SET k v2 KEEPTTL`             | `GET k` is `v2`; deadline unchanged                       | FR-006.2, 006.7 | Unit, Differential  | `set_ttl`: "SET handles an existing TTL like Redis" | Unit |
| TC-096 | `SET` expiry option conflicts    | `SET k v EX 10 PX 10`; `SET k v EX 10 KEEPTTL`; `SET k v EX` | `-ERR syntax error`; `k` unchanged             | FR-006.2, 010  | Unit, Differential   | `set_ttl`: "SET option validation"         | Unit     |
| TC-097 | `SET` repeated option            | `SET k v EX 10 EX 20`                              | `+OK`; the last expiry wins                               | FR-006.2       | Unit                 | `set_ttl`: "SET option validation"         | Unit     |

## 2. Deadlines already past and invalid amounts (AC-01, 02, 03)

| ID        | Scenario                         | Given / When                                       | Then                                                      | Req            | Level                | Existing test                              | Coverage |
|-----------|----------------------------------|----------------------------------------------------|-----------------------------------------------------------|----------------|----------------------|--------------------------------------------|----------|
| TC-098 | Zero or negative amount          | Key `k`; `EXPIRE k 0`; again with `EXPIRE k -5`    | `:1`; `EXISTS k` is `:0`; the log records `DEL` (TC-113) | FR-006.5     | Unit, Differential   | `expiry`: "Deadlines already past, invalid amounts and wrong arity" | Unit |
| TC-099 | Absolute time in the past        | Key `k`; `EXPIREAT k 1`; `PEXPIREAT k 1`           | `:1`; the key is gone                                     | FR-006.5       | Unit, Differential   | `expiry`: "Deadlines already past, invalid amounts and wrong arity" | Unit |
| TC-100 | Not an integer                   | `EXPIRE k abc`; `EXPIRE k 1.5`; `PEXPIRE k ""`     | `-ERR value is not an integer or out of range`            | FR-006.6, 010  | Unit, Differential   | `expiry`: "Deadlines already past, invalid amounts and wrong arity" | Unit |
| TC-101 | Deadline overflows               | `EXPIRE k 9223372036854775807`; `PEXPIRE k 9223372036854775807` | `-ERR invalid expire time`; the previous deadline is kept | FR-006.6 | Unit, Differential | `set_ttl`: "Expiry arguments that overflow the clock are rejected" | Partial |
| TC-102 | `SET` with a non-positive or overflowing expiry | `SET k v EX 0`; `EX -1`; `PX 9223372036854775807` | `-ERR invalid expire time in 'set' command`; `k` unchanged | FR-006.6 | Unit, Differential | `set_ttl`: "SET option validation", "Expiry arguments that overflow the clock are rejected" | Unit |
| TC-103 | Wrong arity                      | `EXPIRE k`; `EXPIRE k 1 2`; `TTL`; `PERSIST k x`   | `-ERR wrong number of arguments for '...' command`        | FR-010         | Unit, Differential   | `expiry`: "Deadlines already past, invalid amounts and wrong arity" | Unit |

## 3. Reading and cancelling (AC-04, 05, 06)

| ID        | Scenario                         | Given / When                                       | Then                                                      | Req            | Level                | Existing test                              | Coverage |
|-----------|----------------------------------|----------------------------------------------------|-----------------------------------------------------------|----------------|----------------------|--------------------------------------------|----------|
| TC-104 | `TTL` rounds, `PTTL` does not    | `SET k v EX 100`, then `TTL k` at once             | `:100` (not 99); `PTTL k` is in 99000..100000             | FR-006.3, NFR-010.4 | Unit, Differential | `set_ttl`: "SET applies each expiration option" | Partial |
| TC-104a | `TTL` and `PTTL` states         | Missing key; key without deadline                  | `:-2`; `:-1` (both commands)                              | FR-006.3       | Unit, Differential   | `keyspace`: "Setting and clearing expiry"  | Unit     |
| TC-104b | `PERSIST`                       | `k` has a deadline; `PERSIST k`; again; on a missing key | `:1`, then `:0`, then `:0`; `TTL k` is `:-1`        | FR-006.4       | Unit, Differential   | `keyspace`: "Setting and clearing expiry"  | Unit     |
| TC-104c | In-place writes keep the deadline | `k` has a deadline; `INCR`, `APPEND`, `LPUSH`, `HSET`, `SADD` on the matching types | `TTL` stays positive after each | FR-006.7   | Unit, Differential   | `expiry`: "Writes keep or drop a deadline" | Unit |
| TC-104d | `MSET` clears the deadline      | `k` has a deadline; `MSET k v`                     | `TTL k` is `:-1`                                          | FR-006.7       | Unit, Differential   | `expiry`: "Writes keep or drop a deadline" | Unit |
| TC-104e | `DEL` and `FLUSHDB` drop the deadline | `k` has a deadline; `DEL k`; `SET k v`        | `TTL k` is `:-1`; same after `FLUSHDB`                    | FR-006.7       | Unit                 | `expiry`: "Writes keep or drop a deadline" | Unit |

## 4. Expiry on access (AC-07)

| ID        | Scenario                         | Given / When                                       | Then                                                      | Req            | Level                | Existing test                              | Coverage |
|-----------|----------------------------------|----------------------------------------------------|-----------------------------------------------------------|----------------|----------------------|--------------------------------------------|----------|
| TC-105 | Every read sees the key missing  | `PEXPIRE k 50`; wait until 50 ms have passed; `GET`, `EXISTS`, `TYPE`, `TTL`, `LLEN`, `HGETALL` as the type allows | `$-1`/`:0`/`none`/`:-2`/empty, never the old value | FR-006.8, NFR-010.1 | Unit, Integration | `expiry`: "A key past its deadline is missing to every command" | Unit |
| TC-106 | `KEYS` skips a due key           | `PEXPIRE k 50`; wait; `KEYS *`                     | `k` is not listed                                         | FR-006.8       | Unit                 | `expiry`: "A key past its deadline is missing to every command" | Unit |
| TC-106a | A write starts from empty       | A list `k` is due; `LPUSH k x`; `LRANGE k 0 -1`    | Only `x`; no `WRONGTYPE`; `TTL k` is `:-1`                | FR-006.8       | Unit                 | `expiry`: "A key past its deadline is missing to every command" | Unit |
| TC-106b | A type change after expiry      | A string `k` is due; `LPUSH k x`                   | Succeeds, not `WRONGTYPE`                                 | FR-006.8       | Unit                 | `expiry`: "A key past its deadline is missing to every command" | Unit |
| TC-106c | `expired_keys` counts lazy removal | Note `expired_keys`; a due key is read; `INFO stats` | Counter rises by 1                                     | FR-006.8, 006.11 | Unit, Integration  | `expiry`: "A key past its deadline is missing to every command" | Unit |

## 5. Active expiry (AC-08)

| ID        | Scenario                         | Given / When                                       | Then                                                      | Req            | Level                | Existing test                              | Coverage |
|-----------|----------------------------------|----------------------------------------------------|-----------------------------------------------------------|----------------|----------------------|--------------------------------------------|----------|
| TC-107 | Unread keys are swept            | `PEXPIRE` 100 keys by 50 ms; read none; wait 300 ms | `DBSIZE` is `:0`; `expired_keys` rose by 100             | FR-006.9, NFR-010.2 | Integration     | `database`: "Expiring keys" (cycle only)   | Partial  |
| TC-108 | The sweep is bounded             | 2500 keys all due; call the cycle with a limit of 1000 | Returns 1000; a second call 1000; a third 500; then 0 | FR-006.9, NFR-010.3 | Unit             | `database`: "Expiring keys"                | Partial  |
| TC-109 | Oldest deadline first            | Keys due at t1 < t2 < t3; limit 2                  | The keys of t1 and t2 go; t3 stays                        | FR-006.9       | Unit                 | `database`: "Expiring keys"                | Partial  |
| TC-110 | The sweep does not touch live keys | Mix of due, future and persistent keys; one cycle | Only the due keys go; the others and their deadlines remain | FR-006.9    | Unit                 | `database`: "Keeping the expiry indexes consistent" | Partial |
| TC-110a | Commands and the sweep never overlap | Several clients run `SET ... PX` and `GET` while the sweep runs | Every reply matches a model; no sanitizer report | NFR-010.6, FR-009 | Stress, ThreadSanitizer | Stress test avoids expiry          | Gap      |

## 6. Persistence (AC-09)

| ID        | Scenario                         | Given / When                                       | Then                                                      | Req            | Level                | Existing test                              | Coverage |
|-----------|----------------------------------|----------------------------------------------------|-----------------------------------------------------------|----------------|----------------------|--------------------------------------------|----------|
| TC-111 | `EXPIRE` logged as absolute      | `--appendonly`; `EXPIRE k 300`                     | The log holds `PEXPIREAT k <ms>`, not `EXPIRE`            | FR-006.10      | Unit                 | `aof`: "Commands that cannot be replayed as received" | Unit |
| TC-112 | `SET ... EX` logged as `PXAT`    | `--appendonly`; `SET k v EX 300`                   | The log holds `SET k v PXAT <ms>` without `NX`, `XX`, `GET` | FR-006.10    | Unit                 | `aof`: "Commands that cannot be replayed as received" | Unit |
| TC-113 | Past deadline logged as `DEL`    | `--appendonly`; `EXPIRE k 0`; `PEXPIREAT k 1`; `SET k v PXAT 1` (`1` is positive, so it is accepted, and past) | The log holds `DEL k`                  | FR-006.10, 006.5 | Unit              | `aof`: "Commands that cannot be replayed as received" | Partial |
| TC-114 | Expired key logged as `DEL`      | `--appendonly`; key expires by access; another by the sweep | The log holds `DEL` for both, once each          | FR-006.10      | Unit                 | `aof`: "A keyspace that tracks its expiry" (keyspace only); "A replay does not depend on when it runs" (removal on access); sweep path not tested | Partial |
| TC-115 | Replay does not extend a lifetime | Log `EXPIRE k 300`; restart 100 s later (or replay with a fixed clock) | `TTL k` is about 200, not 300                  | FR-006.10, NFR-010.5 | Unit, Integration | `aof`: "A replay does not depend on when it runs" checks commands against a passed deadline, not the remaining `TTL` | Partial |
| TC-116 | Nothing expires during replay    | A log sets a key with a deadline already past by the time of replay, then reads it | The commands in the log see the key; expiry resumes after replay | FR-006.10 | Unit | `aof`: "A replay does not depend on when it runs" | Unit |
| TC-117 | Snapshot keeps deadlines         | A key with a deadline; `SAVE`; restart             | After load, `TTL k` matches the remaining time            | FR-006.10, FR-011 | Unit, Integration | `snapshot`: "Snapshots round-trip every value type" | Partial |
| TC-118 | Snapshot skips keys already due  | A key with a short deadline; `SAVE`; restart after it passed | The key is not restored; `DBSIZE` excludes it   | FR-006.10      | Unit, Integration    | None found                                 | Gap      |
| TC-119 | Base of the log keeps a deadline that passed since it was written | A base holds `k` with a deadline; the deadline passes before restart; a logged command uses `k` | On startup the command sees `k`; after the replay `k` expires and a `DEL` is logged | FR-006.10, FR-012 | Unit | None found (`aof`: "Rewriting the log" has no deadlines) | Gap |

## 7. Observability and clock (AC-10)

| ID        | Scenario                         | Given / When                                       | Then                                                      | Req            | Level                | Existing test                              | Coverage |
|-----------|----------------------------------|----------------------------------------------------|-----------------------------------------------------------|----------------|----------------------|--------------------------------------------|----------|
| TC-120 | `INFO keyspace` counts deadlines | 3 keys, 2 with deadlines; `INFO keyspace`          | `db0:keys=3,expires=2`; after `PERSIST` of one, `expires=1` | FR-006.11    | Unit, Integration    | `expiry`: "INFO reports the keys with a deadline and the keys that expired" | Unit |
| TC-121 | `INFO stats` counts expiry       | 5 keys expire (access or sweep); `INFO stats`      | `expired_keys` is 5; `FLUSHDB` does not add to it         | FR-006.11      | Unit, Integration    | `expiry`: "INFO reports the keys with a deadline and the keys that expired" | Unit |
| TC-122 | Trace log of the sweep           | `--log-level trace`; keys expire by the sweep      | `Expired N keys` is logged; no key name appears           | FR-006.9, 3.2.3 | Integration         | None found                                 | Gap      |
| TC-123 | Deadline arithmetic saturates safely | `DeadlineTimer` near the largest time          | No overflow; the commands reject instead (TC-101)      | NFR-010.1      | Unit                 | `deadline_timer`                           | Unit     |

## Known limits: tests to write when they change

Pinned today by `expiry`: "Known limits of key expiry, as they stand" (`test/unit/server/commands/expiry.test.cpp`).

These behaviours are in the specification as they stand. A test that pins them makes a later change deliberate.
A limit that is by design has an ID `LI-nn`; one that is a defect or a missing feature is tracked as a GitHub issue
and cited by its number (list: [issues.md](issues.md)).

| ID        | Behaviour pinned                                                          | Given / When                                  | Today's result                                      |
|-----------|---------------------------------------------------------------------------|-----------------------------------------------|-----------------------------------------------------|
| LI-05 | `DBSIZE` counts a due key not yet swept                                   | A key is due; sweep not yet run; `DBSIZE`     | The key is counted                                  |
| #40 | `DEL` of a due key not yet swept                                          | A key is due; `DEL k`                         | `:1` (real Redis: `:0`)                             |
| #41 | No `NX`/`XX`/`GT`/`LT` on `EXPIRE`                                        | `EXPIRE k 10 NX`                              | `-ERR wrong number of arguments for 'expire' command` |
| #42 | No `EXPIRETIME`/`PEXPIRETIME`                                             | `EXPIRETIME k`                                | `-ERR unknown command 'EXPIRETIME'` (as the server words it) |

## Coverage summary

Criteria to scenarios to coverage is kept in [traceability.md](traceability.md), so there is one table to update.

## Entry and exit criteria

* **Entry**: the server builds; the unit and integration suites run in the chosen preset.
* **Exit**: every scenario is Unit or higher, or has a Gap recorded in the test plan's anomaly list (section 16); the
  scenarios marked ThreadSanitizer pass under the `tsan` preset.
