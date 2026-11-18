# Use Case: Expire Keys

## Use Case ID

UC-011

## Description

Client gives a key a deadline after which it disappears, reads how long it has left, or removes the deadline. A
deadline is set with `EXPIRE`, `PEXPIRE`, `EXPIREAT`, `PEXPIREAT`, or with the expiry options of `SET`
([UC-002](../../data_types/use_cases/uc_002_set_key_value.md)); it is read with `TTL` and `PTTL`, and removed with `PERSIST`.

## Primary Actor

Client (redis-cli or any Redis client)

## Preconditions

* Server is running
* Command syntax is valid:
  * `EXPIRE <key> <seconds>`, `PEXPIRE <key> <milliseconds>`
  * `EXPIREAT <key> <unix-seconds>`, `PEXPIREAT <key> <unix-milliseconds>`
  * `TTL <key>`, `PTTL <key>`, `PERSIST <key>`

## Postconditions

* The key has the new deadline, of any type of value
* Once the deadline passes, the key is gone: reads treat it as missing, and the expiry sweep removes it
* With the append-only file on, the deadline is logged as an absolute `PEXPIREAT`, so a replay later does not extend
  it

## Flow of Events

| Step | Actor  | Description                                                                    |
|------|--------|--------------------------------------------------------------------------------|
| 1    | Client | Sends `EXPIRE session:42 300`                                                  |
| 2    | Server | Parses the request and checks that the amount is an integer                    |
| 3    | Server | Computes the deadline, now plus 300 seconds, and sets it on the key            |
| 4    | Server | Replies `1`                                                                    |
| 5    | Client | Sends `TTL session:42`                                                         |
| 6    | Server | Replies with the seconds left, rounded to the nearest second, here `300`       |
| 7    | Server | When the deadline passes, removes the key on the next access or sweep          |

## Alternate Flows

### A1: Key missing

* `EXPIRE` and its variants reply `0` and change nothing
* `TTL` and `PTTL` reply `-2`

### A2: Key has no deadline

* `TTL` and `PTTL` reply `-1`
* `PERSIST` replies `0`

### A3: Remove the deadline (`PERSIST`)

* Server removes the deadline and replies `1`; the key no longer expires

### A4: Deadline already past

* A zero or negative amount, or a timestamp in the past, deletes the key at once; the reply is `1`, and the
  append-only file logs a `DEL`

### A5: Amount not an integer, or out of range

* Server replies `ERR value is not an integer or out of range`
* An amount whose deadline would overflow gets `ERR invalid expire time in '<command>' command`, the command name in
  lower case (`expire`, `pexpire`, `expireat`), as Redis words it

### A6: Replacing the value

* `SET` without `KEEPTTL` and `MSET` clear the deadline; commands that change a value in place (`INCR`, `APPEND`,
  `LPUSH`, `HSET`, ...) keep it

## Related Functional Requirements

* FR-005: Keyspace commands
* FR-006: Key expiry (FR-006.1 to FR-006.11)
* FR-010: Error replies
* FR-011: Snapshots (deadlines are saved; due keys are skipped on load)
* FR-012: Append-only file

## Related Nonfunctional Requirements

* NFR-001: Performance (the sweep is bounded)
* NFR-003: Reliability (deadlines survive a restart)
* NFR-004: Compatibility
* NFR-010: Key expiry (correctness, timeliness, bounded cost, durability)

## Acceptance Criteria and Diagrams

### AC-01: Expire a key after a delay

Requirements: FR-006.1, FR-006.6

* Given a key `k` of any type, when I send `EXPIRE k 300`, then the reply is `:1` and `k` has a deadline 300 seconds
  ahead. `PEXPIRE` does the same in milliseconds.
* Given no key `k`, when I send `EXPIRE k 300`, then the reply is `:0` and nothing changes.
* Given `EXPIRE k abc`, then the reply is `-ERR value is not an integer or out of range`.
* Given an amount whose deadline would overflow a signed 64-bit millisecond count, then the reply is
  `-ERR invalid expire time in 'expire' command` (`'pexpire'`, `'expireat'` for the others), and the key keeps its
  previous deadline.

### AC-02: Expire a key at a point in time

Requirements: FR-006.1, FR-006.5, FR-006.6

* Given a key `k`, when I send `EXPIREAT k <unix-seconds>` or `PEXPIREAT k <unix-milliseconds>` with a time in the
  future, then the reply is `:1` and the key has that deadline.
* Given a time in the past, then the key is deleted at once and the reply is `:1`.
* Given `EXPIRE k 0` or a negative amount, then the key is deleted at once and the reply is `:1`.

### AC-04: See how long a key has left

Requirements: FR-005, FR-006.3

* Given a key set with `EXPIRE k 100` just now, when I send `TTL k`, then the reply is `:100` (rounded to the nearest
  second, not truncated); `PTTL k` replies in milliseconds.
* Given a key without a deadline, then `TTL` and `PTTL` reply `:-1`.
* Given a missing key, or one whose deadline has passed, then they reply `:-2`.

### AC-05: Cancel a key's expiry

Requirements: FR-005, FR-006.4

* Given a key with a deadline, when I send `PERSIST k`, then the reply is `:1` and `TTL k` replies `:-1`.
* Given a key without a deadline, or a missing key, then `PERSIST` replies `:0`.

### AC-06: Updating a value does not silently change its lifetime

Requirements: FR-006.7

* Given a key with a deadline, then `INCR`, `APPEND`, `LPUSH`, `HSET`, `SADD` and the like keep it.
* Given a key with a deadline, then `SET` without `KEEPTTL`, and `MSET`, clear it.
* Given `DEL` or `FLUSHDB`, then the deadline goes with the key; a key created again later has none.

Setting a deadline with `SET` is AC-03 in [UC-002](../../data_types/use_cases/uc_002_set_key_value.md). Removal of expired keys is
[UC-020](uc_020_remove_expired_keys.md), persistence of deadlines [UC-021](uc_021_keep_deadlines_across_restarts.md),
and the counters [UC-022](uc_022_monitor_key_expiry.md).

Diagrams: [state_key_expiry_lifecycle.puml](../diagrams/state_key_expiry_lifecycle.puml),
[sequence_key_expiry.puml](../diagrams/sequence_key_expiry.puml)

## Notes

* The sweep runs every 100 ms and removes at most 1000 expired keys, oldest deadline first, so a key may stay in memory
  a little past its deadline, but no command ever sees it.
* Expired keys are counted in `INFO stats` (`expired_keys`), and keys with a deadline in `INFO keyspace` (`expires`);
  see [UC-022](uc_022_monitor_key_expiry.md).
* The `EXPIRE` family takes no `NX`, `XX`, `GT` or `LT` option, and there are no `EXPIRETIME` or `PEXPIRETIME`
  commands.
