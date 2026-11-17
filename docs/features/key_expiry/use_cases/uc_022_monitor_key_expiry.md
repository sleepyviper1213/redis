# Use Case: Monitor Key Expiry

## Use Case ID

UC-022

## Description

Operator checks how many keys have expired and how many keys carry a deadline, to confirm keys expire as expected.
Part of [UC-014: Inspect the server](../../observability/use_cases/uc_014_inspect_server.md).

## Primary Actor

Operator (through a client such as redis-cli)

## Preconditions

* Server is running
* Command syntax is valid: `INFO stats`, `INFO keyspace`

## Postconditions

* Nothing changes; the counters are read only

## Flow of Events

| Step | Actor    | Description                                                                              |
|------|----------|------------------------------------------------------------------------------------------|
| 1    | Operator | Sends `INFO stats`                                                                       |
| 2    | Server   | Replies with `expired_keys:<n>`: keys removed because their deadline passed              |
| 3    | Operator | Sends `INFO keyspace`                                                                    |
| 4    | Server   | Replies with `db0:keys=<n>,expires=<m>`: all keys, and keys with a deadline              |

## Alternate Flows

### A1: Keys due but not yet removed

* `keys` and `expires` still count them until an access or the sweep removes them
  ([UC-020](uc_020_remove_expired_keys.md))

### A2: Trace log

* With `--log-level trace`, each sweep that removed keys logs `Expired N keys`, never the key names

## Related Functional Requirements

* FR-006: Key expiry (FR-006.11)
* FR-007: Server commands

## Related Nonfunctional Requirements

* NFR-010: Key expiry (NFR-010.7 observability)

## Acceptance Criteria and Diagrams

### AC-10: See what expiry did

Requirements: FR-006.11, FR-007, NFR-010.7

* `INFO stats` shows `expired_keys`: keys removed because their deadline passed, by access or by the sweep.
* `INFO keyspace` shows `db0:keys=<n>,expires=<m>`: keys, and keys with a deadline, each counting keys that are due
  but not yet removed.

## Notes

* `FLUSHDB` and `DEL` do not add to `expired_keys`.
