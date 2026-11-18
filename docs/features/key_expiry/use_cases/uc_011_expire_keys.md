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
  * `EXPIRE <key> <seconds> [NX | XX | GT | LT]`, `PEXPIRE <key> <milliseconds> [NX | XX | GT | LT]`
  * `EXPIREAT <key> <unix-seconds> [NX | XX | GT | LT]`, `PEXPIREAT <key> <unix-milliseconds> [NX | XX | GT | LT]`
  * `TTL <key>`, `PTTL <key>`, `PERSIST <key>`

## Postconditions

* The key has the new deadline, of any type of value
* Once the deadline passes, the key is gone: reads treat it as missing, and the expiry sweep removes it
* With the append-only file on, the deadline is logged as an absolute `PEXPIREAT`, so a replay later does not extend
  it; a command that did not apply (an option said no) is not logged

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

* For `EXPIRE`, `PEXPIRE`, `EXPIREAT` and `PEXPIREAT`, a zero or negative amount, or a timestamp in the past, deletes
  the key at once; the reply is `1`, and the append-only file logs a `DEL`
* `SET` with a past `EXAT` or `PXAT` does not delete: see [UC-002](../../data_types/use_cases/uc_002_set_key_value.md) AC-03

### A5: Amount not an integer, or out of range

* Server replies `ERR value is not an integer or out of range`
* An amount whose deadline would overflow gets `ERR invalid expire time in '<command>' command`, the command name in
  lower case (`expire`, `pexpire`, `expireat`), as Redis words it

### A6: Replacing the value

* `SET` without `KEEPTTL` and `MSET` clear the deadline; commands that change a value in place (`INCR`, `APPEND`,
  `LPUSH`, `HSET`, ...) keep it

### A7: Conditional deadline (`NX`, `XX`, `GT`, `LT`)

* The options may follow the amount, in any case and any number. `NX` sets the deadline only if the key has none, `XX`
  only if it has one, `GT` only if the new deadline is later than the current one, `LT` only if it is earlier. A key
  without a deadline counts as infinite for `GT` (never applies) and `LT` (applies)
* The reply is `1` if the deadline was set and `0` if not, also for a missing key; nothing changes on `0`
* An argument that is not one of the four gets `ERR Unsupported option <arg>`; `NX` with `XX`, `GT` or `LT` gets
  `ERR NX and XX, GT or LT options at the same time are not compatible`; `GT` with `LT` gets
  `ERR GT and LT options at the same time are not compatible`. The options are checked before the amount, so
  `EXPIRE k abc 11` is an unsupported option, not "not an integer"
* A deadline already past that passes the options still deletes the key, as in A4

## Related Functional Requirements

* FR-005: Keyspace commands
* FR-006: Key expiry (FR-006.1 to FR-006.12)
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

### AC-11: Set a deadline only under a condition

Requirements: FR-006.1, FR-006.12

* Given a key with a deadline 100 seconds ahead, when I send `EXPIRE k 50 GT`, then the reply is `:0` and the deadline is
  unchanged; with `EXPIRE k 200 GT` the reply is `:1` and the deadline is 200 seconds ahead.
* Given a key with a deadline, then `EXPIRE k 10 NX` replies `:0`; `EXPIRE k 10 XX` replies `:1`; `LT` sets it only if
  the new deadline is earlier.
* Given a key without a deadline, then `NX` and `LT` reply `:1`, and `XX` and `GT` reply `:0` (no deadline counts as
  infinite).
* Given a missing key, then any option replies `:0`.
* Given `PEXPIRE`, `EXPIREAT` or `PEXPIREAT`, then the options work the same way on their deadline.
* Given `EXPIRE k 10 11`, then the reply is `-ERR Unsupported option 11`, also when the amount is invalid.
* Given `NX` with `XX`, `GT` or `LT`, or `GT` with `LT`, then the reply is the incompatibility error of Redis.
* Given a command that was applied, then the append-only file logs `PEXPIREAT`; given one that was not, it logs
  nothing.

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
* There are no `EXPIRETIME` or `PEXPIRETIME` commands (#42).
