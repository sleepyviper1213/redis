# Keyspace: Test Scenarios

Test scenarios for deleting keys and inspecting the keyspace, derived from the flows and alternate flows of the
[use cases](use_cases/) of the [Keyspace feature](README.md) (FR-005). They extend the
[Master Test Plan](../../test/test_plan_and_report.md), whose section 12 lists FR-005 as covered.

Scenario IDs are `TC-nnn`, one sequence for the whole project ([ID ranges](../README.md#id-ranges)). Replies are quoted as the server writes them (RESP2 notation: `:1` integer, `$-1` null
bulk, `*0` empty array, `-ERR ...` error). Integration tests named `TC-022` to `TC-027` use the master plan's own
numbering; they are not scenarios of this document. FR-005 also lists `TTL`, `PTTL` and `PERSIST`; their scenarios
belong to [key_expiry](../key_expiry/test_plan.md) and are only pointed to here (TC-085).

## How to read the coverage column

| Mark        | Meaning                                                                                      |
|-------------|----------------------------------------------------------------------------------------------|
| Unit        | A unit test whose scenario name or body matches; named in the *Existing test* column         |
| Integration | No unit test, but an integration test against a running server matches                       |
| Stress      | No unit test, but a stress test against a model matches                                      |
| Partial     | A test covers part of the scenario (named in the column)                                     |
| Gap         | No test found; to be written at the level in the *Level* column                              |

Coverage was matched by test and scenario names in `test/unit/server/commands/keyspace.test.cpp`,
`test/unit/core/utils/glob.test.cpp`, `test/integration/del.test.cpp` and `test/stress/`, and the bodies of the
keyspace command tests were opened to confirm; not every assertion was read. Confirm when a scenario is written or
run. The differential test (`scripts/diff_test.py`) sends `DEL`, `EXISTS`, `KEYS *`, `TYPE`, `DBSIZE` and
`FLUSHDB` against real Redis; it is not tied to a named scenario, so it never decides a mark by itself.

## Levels

| Level        | Use for                                                                                   |
|--------------|-------------------------------------------------------------------------------------------|
| Unit         | One command through the handler against a keyspace, no socket (`test::Server`)            |
| Integration  | A running server through hiredis: sockets, framing, binary-safe key names                 |
| Stress       | Concurrent clients against a model                                                        |
| Differential | Replies compared with real Redis; `KEYS` replies compared sorted                          |

## 1. Deleting keys (UC-004)

| ID       | Behaviour                          | Given / When                                         | Expected                                                    | Level                | Existing test                                                       | Coverage    |
|----------|------------------------------------|------------------------------------------------------|-------------------------------------------------------------|----------------------|---------------------------------------------------------------------|-------------|
| TC-065 | Delete an existing key of each type | A string, list, hash and set; `DEL` each            | `:1` each; `EXISTS` is `:0` afterwards                      | Unit, Integration, Differential | `keyspace`: "Deleting and checking keys" (string keys); integration `TC-022` | Partial |
| TC-066 | Several keys, missing ones skipped | `a` exists; `DEL a missing`; `DEL a b` with both existing | `:1`; `:2`; missing keys are not counted               | Unit, Integration, Differential | `keyspace`: "Deleting and checking keys"; integration `TC-024`      | Unit        |
| TC-067 | Deleting twice is not an error     | `DEL k`; `DEL k`                                     | `:1`, then `:0`                                             | Integration          | integration `TC-023`                                                | Integration |
| TC-068 | Empty and binary key names         | `DEL ""` with no such key; `DEL` of a key with a binary name | `:0`; `:1`                                          | Integration          | integration `TC-026`, `TC-027`                                      | Integration |
| TC-069 | `DEL` with no key                  | `DEL`                                                | `-ERR wrong number of arguments for 'del' command`          | Integration          | integration `TC-025`                                                | Integration |
| TC-070 | `DEL` drops the deadline           | `k` has a deadline; `DEL k`; `SET k v`               | `TTL k` is `:-1` (TC-104e)                               | Unit                 | `database`: "Keeping the expiry indexes consistent"                 | Partial     |
| TC-071 | `DEL`'s reply matches existence under concurrency | Several clients `SET`, `DEL` and `EXISTS` shared keys | A `DEL` returns `:1` only if the key existed in the model | Stress          | `stress` (`DEL` and `EXISTS` operations); `linearizability`: "DEL's reply must match the key's existence" (the checker) | Stress |
| TC-072 | `FLUSHDB` empties the keyspace     | Keys, one with `EX 100`; `FLUSHDB`; `DBSIZE`; `TTL c` | `+OK`; `:0`; `:-2`                                         | Unit, Differential   | `keyspace`: "Deleting and checking keys"                            | Unit        |
| TC-073 | `FLUSHDB` ignores `ASYNC` and `SYNC` | Keys; `FLUSHDB ASYNC`; `FLUSHDB SYNC`              | `+OK`; the keyspace is cleared at once each time            | Unit                 | None found                                                          | Gap         |
| TC-074 | Deletions are logged               | `--appendonly`; `DEL k`; `FLUSHDB`; restart          | The log holds `DEL k` and `FLUSHDB`; replay leaves the keyspace empty | Unit       | `aof`: "Write commands are logged" (`DEL` only)                     | Partial     |
| TC-075 | Last element removed deletes the key | A list, hash or set with one element; pop, `HDEL`, `SREM` or `SPOP` it | `EXISTS` is `:0`; `TYPE` is `none`           | Unit                 | `list`, `hash`, `set` command tests; `database`: "Removing empty collections" (see also TC-048, 054, 056, 058) | Unit |

## 2. Inspecting the keyspace (UC-012)

| ID       | Behaviour                          | Given / When                                         | Expected                                                    | Level                | Existing test                                                       | Coverage    |
|----------|------------------------------------|------------------------------------------------------|-------------------------------------------------------------|----------------------|---------------------------------------------------------------------|-------------|
| TC-076 | `EXISTS` counts keys               | `a`, `b` exist; `EXISTS a b missing`; `EXISTS a a`   | `:2`; `:2` (a key named twice counts twice)                 | Unit, Differential   | `keyspace`: "Deleting and checking keys" (the repeated key not seen) | Partial    |
| TC-077 | `TYPE` of each value type          | A string, list, hash, set and a missing key; `TYPE` each | `+string`, `+list`, `+hash`, `+set`, `+none`            | Unit, Differential   | `string`: "Commands reject keys holding another type" (`list`, `none`, `string` only) | Partial |
| TC-078 | `DBSIZE` counts keys               | Two keys; `DBSIZE`; `FLUSHDB`; `DBSIZE`              | `:2`; `:0`                                                  | Unit, Differential   | `keyspace`: "Deleting and checking keys"                            | Unit        |
| TC-079 | `KEYS` with a namespace pattern    | `user:1`, `user:2`, `post:1`; `KEYS user:*`          | Two keys, `post:1` absent                                   | Unit, Differential   | `keyspace`: "Listing keys by pattern"                               | Unit        |
| TC-080 | Glob syntax                        | `KEYS` with `?`, `[abc]`, `[^a]`, `[a-z]`, `\*`      | Keys match as in Redis; a hostile pattern does not take exponential time | Unit      | `glob`: "Matching keys against literal and wildcard patterns", "Matching keys against character classes", "Escaping special characters", "Pathological patterns do not backtrack exponentially" (matcher only; the command is run with `*` and `user:*`) | Partial |
| TC-081 | No key matches                     | `KEYS nomatch*`; `KEYS *` on an empty keyspace       | `*0`                                                        | Unit, Differential   | None found                                                          | Gap         |
| TC-082 | Due keys are not reported          | `PEXPIRE k 20`; wait; `EXISTS k`, `KEYS *`, `TYPE k`, `DBSIZE` | `:0`; not listed; `+none`; the count excludes it once swept (an unswept key is still counted, LI-05) | Unit | `keyspace`: "Setting and clearing expiry" (`EXISTS` only); `database`: "Expiring keys" | Partial |
| TC-083 | Wrong arity                        | `EXISTS`; `KEYS`; `TYPE`; `TYPE a b`; `DBSIZE x`     | `-ERR wrong number of arguments for '<command>' command`    | Unit, Differential   | `set_ttl`: "Malformed requests get RESP error replies" (generic arity, not these commands) | Partial |
| TC-084 | Inspection leaves data unchanged   | Random writes of all types, then `KEYS *`, `EXISTS`, `TYPE`, `DBSIZE` | Replies match real Redis; no key changes             | Differential         | `scripts/diff_test.py` (keyspace family)                            | Partial     |

## 3. Related commands owned by key expiry

| ID       | Behaviour                          | Given / When                                         | Expected                                                    | Level                | Existing test                                                       | Coverage    |
|----------|------------------------------------|------------------------------------------------------|-------------------------------------------------------------|----------------------|---------------------------------------------------------------------|-------------|
| TC-085 | `TTL`, `PTTL`, `PERSIST` (FR-005)  | Persistent, expiring and missing keys                | `:-1`, `:-2`, `:1`/`:0` as in TC-104a and 104b            | Unit                 | `keyspace`: "Setting and clearing expiry"                           | Unit        |

Coverage per use case is in [traceability.md](traceability.md).

## Known limits: tests to write when they change

| ID       | Behaviour pinned                                                           | Today's result                                      |
|----------|----------------------------------------------------------------------------|-----------------------------------------------------|
| LI-02 | `KEYS` blocks every other client while it walks the keyspace               | A slow `KEYS` is reported by the command watchdog   |
| LI-03 | `FLUSHDB` clears at once whatever the option                               | `ASYNC` and `SYNC` are accepted and ignored (TC-073) |
| #40 | `DEL` of a due key not yet swept                                           | `:1` (real Redis: `:0`; tracked in #40)                  |

## Entry and exit criteria

* **Entry**: the server builds; the unit suite runs in the chosen preset; for the integration scenarios a server can
  be started on a free port.
* **Exit**: every scenario is Unit or higher, or has a Gap recorded in the test plan's anomaly list (section 16);
  the differential and stress runs pass on Linux with `--io-threads 1` and with `--io-threads 4`.
