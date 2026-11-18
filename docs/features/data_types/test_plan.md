# Data Types: Test Scenarios

Test scenarios for strings, lists, hashes and sets, derived from the flows and alternate flows of the
[use cases](use_cases/) of the [Data types feature](README.md) (FR-001 to FR-004). They extend the
[Master Test Plan](../../test/test_plan_and_report.md), whose section 12 lists FR-001 to FR-004 as covered.

Scenario IDs are `TC-nnn`, one sequence for the whole project ([ID ranges](../README.md#id-ranges)). Replies are quoted as the server writes them (RESP2 notation: `:1` integer, `$-1` null
bulk, `*-1` null array, `-ERR ...` error). Integration tests named `TC-007` to `TC-021` use the master plan's own
numbering; they are not scenarios of this document. Where a behaviour belongs to another feature, the scenario points
to its plan instead of repeating it (key expiry: [TC-EXP](../key_expiry/test_plan.md)).

## How to read the coverage columns

Two columns, one question each:

| Column | Meaning |
|--------|---------|
| *Level* | The levels the scenario is designed for. A plan, not a record of what runs |
| *Tested at* | The levels where a test exists today, found from the *Existing test* column: `Unit`, `Integration`, `Stress`, `Differential`, or `none` |
| *Status* | `Covered` when every level in *Level* is in *Tested at*; `Partial` when some are; `Gap` when none are |

So a row with Level `Unit, Integration`, Tested at `Unit` and Status `Partial` has its unit test and lacks its
integration test. `Differential` counts as tested only where the differential script sends the scenario and the run is
gating.

## Levels

| Level        | Use for                                                                                   |
|--------------|-------------------------------------------------------------------------------------------|
| Unit         | One command through the handler against a keyspace, no socket (`test::Server`)            |
| Integration  | A running server through hiredis: sockets, framing, binary-safe bytes                     |
| Stress       | Concurrent clients against a model; atomicity and ordering                                |
| Differential | Replies compared with real Redis, order-independent where Redis defines no order          |

## 1. SET (UC-002)

| ID | Behaviour | Given / When | Expected | Level | Existing test | Tested at | Status |
|----------|------------------------------------|------------------------------------------------------|-------------------------------------------------------------|----------------------|---------------------------------------------------------------------|-------------|----------|
| TC-028 | Store, replace, replace another type | `SET k v`; `SET k w`; then `k` is a list and `SET k v` | `+OK` each time; `GET k` is the last value; `TYPE k` is `string` | Unit, Integration, Differential | integration `TC-008`, `TC-010`; `set_ttl`: "A plain SET overwrites without looking at the old value"; `string`: "Commands reject keys holding another type" | Integration | Partial |
| TC-029 | Empty, special, binary and large values | `SET` with an empty value, an empty key, special characters in the key, a binary value, a value over 1 MB | `+OK`; `GET` returns exactly the bytes sent | Integration | integration `TC-007`, `TC-009`, `TC-012`, `TC-013`, `TC-014` | Integration | Covered |
| TC-030 | `NX`, `XX` and `GET` | `SET k v XX` on a missing key; `SET k v NX` twice; `SET k v GET` twice; `SET k v GET` on a list key | `$-1` for skipped writes; only the first `NX` writes; `GET` returns the previous value; `-WRONGTYPE` on the list | Unit, Differential | `set_ttl`: "SET with NX, XX and GET"; "A plain SET overwrites without looking at the old value" (NX, XX, GET on a list key) | Unit | Partial |
| TC-031 | Expiry options and `KEEPTTL` | `SET k v EX/PX/EXAT/PXAT`; `SET k v` on a key with a deadline; `SET k v KEEPTTL` | Deadline set, cleared or kept as the option says (detail: TC-093 to 095) | Unit, Differential | `set_ttl`: "SET applies each expiration option", "SET handles an existing TTL like Redis" | Unit | Partial |
| TC-032 | Invalid options and arity | `SET k v EX 10 PX 10`; `SET k v EX`; `SET k v EX 0`; `SET k`; `SET` | `-ERR syntax error`; `-ERR invalid expire time in 'set' command`; `-ERR wrong number of arguments for 'set' command`; `k` unchanged (detail: TC-096, 102) | Unit, Integration, Differential | `set_ttl`: "SET option validation", "Expiry arguments that overflow the clock are rejected", "Malformed requests get RESP error replies"; integration `TC-011` | Integration | Partial |
| TC-033 | `SET` is logged | `--appendonly`; `SET k v`; `SET k v EX 100` | The log holds `SET k v`; the `EX` form as `PXAT <ms>` | Unit | `aof`: "Write commands are logged", "Commands that cannot be replayed as received" | Unit | Covered |

## 2. GET (UC-003)

| ID | Behaviour | Given / When | Expected | Level | Existing test | Tested at | Status |
|----------|------------------------------------|------------------------------------------------------|-------------------------------------------------------------|----------------------|---------------------------------------------------------------------|-------------|----------|
| TC-034 | Read a stored value | `SET k v`; `GET k`; after an overwrite; empty and binary values | The value as a bulk string; the new value; the exact bytes | Unit, Integration, Differential | integration `TC-015`, `TC-017`, `TC-018`, `TC-019`; `string` (`GET` after `INCR`) | Integration | Partial |
| TC-035 | Missing or expired key | `GET missing`; `GET ""`; `PEXPIRE k 20`, wait, `GET k` | `$-1`; an expired key is also removed from memory (TC-106c) | Unit, Integration, Differential | integration `TC-016`, `TC-021`; `set_ttl`: "SET applies each expiration option" (key gone after `PX`) | Integration | Partial |
| TC-036 | Another type; wrong arity | `k` is a list; `GET k`; `GET`; `GET a b` | `-WRONGTYPE Operation against a key holding the wrong kind of value`; `-ERR wrong number of arguments for 'get' command` | Unit, Integration, Differential | `string`: "Commands reject keys holding another type"; integration `TC-020` (no key only) | Integration | Partial |

## 3. Several keys, append and counters (UC-007)

| ID | Behaviour | Given / When | Expected | Level | Existing test | Tested at | Status |
|----------|------------------------------------|------------------------------------------------------|-------------------------------------------------------------|----------------------|---------------------------------------------------------------------|-------------|----------|
| TC-037 | `MSET` then `MGET` | `MSET a 1 b 2`; `MGET a b missing` | `+OK`; array `1`, `2`, nil | Unit, Differential | `string`: "Setting and getting several keys at once" | Unit | Partial |
| TC-038 | `MSET` and `MGET` edge cases | `l` is a list and `k` has a deadline: `MSET l v k v`; `MGET l a`; `MSET a 1 b` | Both keys become strings and `TTL k` is `:-1` (TC-104d); `MGET` gives nil for a key that is not a string; the odd `MSET` gets `-ERR wrong number of arguments for 'mset' command` | Unit, Differential | `string`: "Setting and getting several keys at once" (odd arity only) | Unit | Partial |
| TC-039 | `APPEND` and `STRLEN` | `APPEND s Hello` on a missing key; `APPEND s " World"`; `STRLEN s`; `STRLEN missing` | `:5`; `:11`; `:11`; `:0` | Unit, Differential | `string`: "Appending to strings" (`STRLEN` of a missing key not seen) | Unit | Partial |
| TC-040 | Counters start at zero | `INCR n`; `INCRBY n 10`; `DECRBY n 20`; `DECR n`; `GET n` | `:1`, `:11`, `:-9`, `:-10`; stored as the decimal string `-10` | Unit, Differential | `string`: "Counting with INCR and DECR" | Unit | Partial |
| TC-041 | Value or delta is not an integer | `SET s abc`; `INCR s`; `INCRBY n` with `1.5`, `+5`, a leading space, `0x10`, empty, past int64 | `-ERR value is not an integer or out of range`; value unchanged | Unit, Differential | `string`: "Counting with INCR and DECR", "Integer arguments are parsed the way Redis does" | Unit | Partial |
| TC-042 | Overflow | `n` is `9223372036854775807`; `INCR n`; `n` at the minimum; `DECR n`, `DECRBY n` | `-ERR increment or decrement would overflow`; value unchanged | Unit, Differential | `string`: "Counting with INCR and DECR" (`INCR` at the maximum only) | Unit | Partial |
| TC-043 | Counters and `APPEND` keep the deadline | `k` has a deadline; `INCR k`; `APPEND k x` | `TTL k` stays positive (TC-104c) | Unit | None found | none | Gap |
| TC-044 | Wrong type and wrong arity | `l` is a list: `INCR`, `DECR`, `INCRBY`, `DECRBY`, `APPEND`, `STRLEN` on `l`; `INCR`; `INCRBY n`; `APPEND k`; `MGET` | `-WRONGTYPE ...` each; `-ERR wrong number of arguments for '<command>' command` | Unit, Differential | `string`: "Commands reject keys holding another type" (`INCR` only); `set_ttl`: "Malformed requests get RESP error replies" (generic arity) | Unit | Partial |
| TC-045 | Counters are atomic | Several clients `INCR` one key at once | The final value equals the number of increments | Stress | `stress`: `INCR` on a shared counter key; final value check not read | Stress | Covered |

## 4. Lists (UC-008)

| ID | Behaviour | Given / When | Expected | Level | Existing test | Tested at | Status |
|----------|------------------------------------|------------------------------------------------------|-------------------------------------------------------------|----------------------|---------------------------------------------------------------------|-------------|----------|
| TC-046 | Push at both ends | `RPUSH l b c`; `LPUSH l a`; `LRANGE l 0 -1`; `LLEN l` | `:2`, `:3`; `a b c`; `:3` | Unit, Stress, Differential | `list`: "Pushing to and popping from lists"; stress (`LPUSH`, `RPUSH`, `LRANGE` sweep) | Stress | Partial |
| TC-047 | `LPUSH` with several elements | `LPUSH l a b c`; `LRANGE l 0 -1` | The list reads `c b a` | Unit, Differential | None found | none | Gap |
| TC-048 | Pops: ends, count, missing key | List `a b c`: `LPOP`, `RPOP`; then `LPOP l 10`; `LPOP missing`; `LPOP missing 5`; `RPOP missing 0` | `a`; `c`; the rest and the list is deleted; `$-1`; `*-1`; `*-1` | Unit, Differential | `list`: "Pushing to and popping from lists" | Unit | Partial |
| TC-049 | Invalid pop count | `LPOP l -1`; `LPOP l x`; `RPOP l 1.5` | `-ERR value is out of range, must be positive` | Unit | None found | none | Gap |
| TC-050 | Ranges and indexes | List `a b c d e`: `LRANGE l -2 -1`, `-100 1`, `3 100`, `10 20`, `3 1`; `LINDEX l -1`, `LINDEX l 5` | Tail pair; clamped; empty arrays; last element; `$-1` | Unit, Differential | `list`: "Reading list ranges and indexes" | Unit | Partial |
| TC-051 | Missing key, wrong type, bad integers, arity | `LRANGE missing 0 -1`; `LPUSH` on a string; `LRANGE l a 1`; `LINDEX l x`; `LPUSH l`; `LLEN` | `*0`; `-WRONGTYPE ...`; `-ERR value is not an integer or out of range`; arity error | Unit, Differential | `list`: "Reading list ranges and indexes" (`LINDEX` missing key and wrong type; non-integer indexes) | Unit | Partial |

## 5. Hashes (UC-009)

| ID | Behaviour | Given / When | Expected | Level | Existing test | Tested at | Status |
|----------|------------------------------------|------------------------------------------------------|-------------------------------------------------------------|----------------------|---------------------------------------------------------------------|-------------|----------|
| TC-052 | Set and read fields | `HSET h f1 v1 f2 v2`; `HGET`, `HLEN`, `HEXISTS`, `HMGET h f1 nope`; `HSET h f1 new` | `:2`; `v1`; `:2`; `:1`; `v1` and nil; the overwrite gives `:0` and `HGET` returns `new` | Unit, Differential | `hash`: "Writing and reading hash fields" | Unit | Partial |
| TC-053 | Missing key or field; `HGETALL` | `HGET h nope`; `HLEN missing`; `HEXISTS missing f`; `HGETALL missing`; `HGETALL h` | `$-1`; `:0`; `:0`; `*0`; field and value pairs in any order | Unit, Differential | `hash` (`HMGET` nil only); no `HGETALL` test found | Unit | Partial |
| TC-054 | `HDEL` and the empty hash | `HDEL h f1 f2 nope`; `EXISTS h` | Count of fields removed, missing ones not counted; the empty hash is deleted | Unit, Differential | `hash`: "Writing and reading hash fields" (all fields, none missing) | Unit | Partial |
| TC-055 | Wrong type and wrong arity | `HSET h f1 v1 f2`; `HSET` on a list; `HGET h`; `HDEL h` | `-ERR wrong number of arguments for 'hset' command`; `-WRONGTYPE ...`; arity errors | Unit, Differential | `hash` (`HSET` odd arity); `string`: "Commands reject keys holding another type" (`HSET`) | Unit | Partial |

## 6. Sets (UC-010)

| ID | Behaviour | Given / When | Expected | Level | Existing test | Tested at | Status |
|----------|------------------------------------|------------------------------------------------------|-------------------------------------------------------------|----------------------|---------------------------------------------------------------------|-------------|----------|
| TC-056 | Add and remove members | `SADD s a b a`; `SCARD`; `SISMEMBER s a`, `s z`; `SREM s a b z`; `EXISTS s` | `:2`; `:2`; `:1`; `:0`; `:2`; `:0` (the empty set is deleted) | Unit, Stress, Differential | `set`: "Adding and removing set members"; stress (`SADD`, `SREM`, `SMEMBERS` sweep) | Stress | Partial |
| TC-057 | Intersection | `x = {a b c}`, `y = {b c d}`: `SINTER x y`; `SINTER x missing`; `SINTER missing str` (string) | `b` and `c` in any order; `*0`; `-WRONGTYPE ...` (every key is type-checked) | Unit, Differential | `set`: "Intersecting sets" | Unit | Partial |
| TC-058 | `SPOP` with and without a count | Set `a b c`: `SPOP s`; `SPOP s 5` on three members | One member, removed; an array of all members and the set is deleted | Unit | `set`: "Popping random members" | Unit | Covered |
| TC-059 | `SPOP` on a missing key; invalid count | `SPOP missing`; `SPOP missing 3`; `SPOP s -1`; `SPOP s x` | `$-1`; `*0`; `-ERR value is out of range, must be positive` | Unit | None found | none | Gap |
| TC-060 | `SPOP` is logged as `SREM` | `--appendonly`; `SPOP s 2`; restart | The log holds `SREM`, not `SPOP`; replay leaves the same member | Unit | `aof`: "Commands that cannot be replayed as received" | Unit | Covered |
| TC-061 | Missing key, wrong type, arity | `SISMEMBER missing a`; `SCARD missing`; `SMEMBERS missing`; `SADD` on a list; `SADD s`; `SINTER` | `:0`; `:0`; `*0`; `-WRONGTYPE ...`; arity errors | Unit, Differential | `string`: "Commands reject keys holding another type" (`SADD` only) | Unit | Partial |

## 7. Across types

| ID | Behaviour | Given / When | Expected | Level | Existing test | Tested at | Status |
|----------|------------------------------------|------------------------------------------------------|-------------------------------------------------------------|----------------------|---------------------------------------------------------------------|-------------|----------|
| TC-062 | List, hash and set writes are logged | `--appendonly`; `RPUSH`, `LPOP`, `HSET`, `HDEL`, `SADD`, `SREM`; restart | Replay leaves the same collections | Unit | `aof`: "A restart gets back what was logged" (value types not read); `SPOP` only in TC-060 | Unit | Covered |
| TC-063 | Replies match Redis over random commands | The differential test sends random string, list, set and hash commands with invalid arguments and mixed types | No reply differs from real Redis | Differential | `scripts/diff_test.py` (all four families; `SPOP` is not sent; `SET ... EX/PX` only with long expiries) | Differential | Covered |
| TC-064 | Hashes under concurrency | Several clients `HSET` and `HGETALL` shared keys | Every reply matches a model | Stress | None found (the stress model has strings, lists and sets, no hashes) | none | Gap |

Coverage per use case is in [traceability.md](traceability.md).

## Known limits: tests to write when they change

| ID       | Behaviour pinned                           | Given / When      | Today's result                                           |
|----------|--------------------------------------------|-------------------|----------------------------------------------------------
| LI-01 | Blocking pops are not supported            | `BLPOP l 0`       | An unknown-command error, worded as the server words it |

## Entry and exit criteria

* **Entry**: the server builds; the unit suite runs in the chosen preset; for the integration scenarios a server can
  be started on a free port.
* **Exit**: every scenario is Unit or higher, or has a Gap recorded in the test plan's anomaly list (section 16);
  the differential and stress runs pass on Linux with `--io-threads 1` and with `--io-threads 4`.
