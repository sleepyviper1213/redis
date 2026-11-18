# Use Case: Remove Expired Keys

## Use Case ID

UC-020

## Description

A key whose deadline has passed stops existing for clients at once, and its memory is freed either by the next command
that looks it up or by a periodic sweep. Deadlines are set in [UC-011](uc_011_expire_keys.md).

## Primary Actor

Server (the periodic sweep, and any command that looks a key up). The Client benefits and the Operator sees the effect
in memory and in `INFO` ([UC-022](uc_022_monitor_key_expiry.md)).

## Preconditions

* Server is running
* A key has a deadline, and the clock has passed it
* Expiry is on (it is off only while the append-only file is replayed, [UC-021](uc_021_keep_deadlines_across_restarts.md))

## Postconditions

* No command observes the key: reads see it as missing, `KEYS` does not list it, a write starts from an empty key
* The key, its value and its deadline are removed from memory, by access or by the sweep
* `expired_keys` is incremented once per key
* With the append-only file on, a `DEL` is logged for the key

## Flow of Events

### Main flow: the sweep

| Step | Actor  | Description                                                                              |
|------|--------|------------------------------------------------------------------------------------------|
| 1    | Server | Every 100 ms the sweep runs on the keyspace thread, between commands                     |
| 2    | Server | Takes the key with the oldest deadline; stops if its deadline is still in the future     |
| 3    | Server | Erases the key, its value and its deadline, and counts it in `expired_keys`              |
| 4    | Server | Repeats from step 2 until 1000 keys are removed or none is due                           |
| 5    | Server | With the append-only file on, appends a `DEL` for each key removed                       |
| 6    | Server | Logs `Expired N keys` at trace level, if N is above zero                                 |

## Alternate Flows

### A1: A command looks the key up first (lazy removal)

* The command erases the key, counts it, and sees it as missing: `GET` replies a null bulk string, `EXISTS` `0`,
  `TYPE` `none`, `TTL` `-2`
* A write (`SET`, `LPUSH`, ...) starts from an empty key, not from the expired value, and any type is allowed

### A2: More than 1000 keys are due

* One run removes 1000, oldest deadline first; the rest wait for the next run, so the sweep never holds up commands
  for long

### A3: Expiry is off (replay)

* Nothing is removed while the append-only file is replayed; expiry resumes when the replay ends

## Related Functional Requirements

* FR-006: Key expiry (FR-006.8 lazy expiry, FR-006.9 active expiry)
* FR-009: Concurrent clients (the sweep runs on the keyspace thread)
* FR-012: Append-only file

## Related Nonfunctional Requirements

* NFR-010: Key expiry (NFR-010.1 correctness, 010.2 timeliness, 010.3 bounded cost, 010.6 safety)

## Acceptance Criteria and Diagrams

### AC-07: An expired key is gone, at once

Requirements: FR-006.8, NFR-010.1

* Given a key past its deadline that has not yet been swept, then `GET`, `EXISTS`, `TYPE`, `TTL` and the typed
  commands treat it as missing, and `KEYS` does not list it.
* Given such a key, when a command writes it (`SET`, `LPUSH`, ...), then the write starts from an empty key, not from
  the expired value.

### AC-08: Expired keys that nobody reads leave memory

Requirements: FR-006.9, NFR-010.2, NFR-010.3, NFR-010.6

* Given keys past their deadline that nobody reads, then the sweep removes them by its first run after the deadline,
  at most 100 ms later, oldest deadline first.
* Given more than 1000 keys due, then one run removes 1000 and the rest wait for later runs; the sweep never runs
  unbounded.
* Given the sweep removed keys, then the trace log says `Expired N keys`.

Diagrams: [activity_expiry_sweep.puml](../diagrams/activity_expiry_sweep.puml),
[state_key_expiry_lifecycle.puml](../diagrams/state_key_expiry_lifecycle.puml),
[sequence_key_expiry.puml](../diagrams/sequence_key_expiry.puml)

## Notes

* A key may stay in memory a little past its deadline, so `DBSIZE` and `expires` may count it until it is removed.
* `DEL` removes a due key without checking its deadline first, so it may reply `1` for a key that other commands
  already see as missing.
