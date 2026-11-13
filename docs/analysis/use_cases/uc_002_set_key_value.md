# Use Case: Set Key-Value Pair

## Use Case ID

UC-002

## Description

Client stores or replaces a string value with `SET`, optionally with an expiry or a condition.

## Primary Actor

Client (redis-cli or any Redis client)

## Preconditions

* Server is running
* Command syntax is valid:
  `SET <key> <value> [NX | XX] [GET] [EX s | PX ms | EXAT ts | PXAT ms-ts | KEEPTTL]`

## Postconditions

* The key holds the new string value, whatever type it held before
* With an expiry option, the key has a deadline; with `KEEPTTL`, it keeps the one it had; otherwise any previous
  deadline is cleared
* With the append-only file on, the write is logged (an `EX` or `PX` expiry as an absolute `PXAT`)

## Flow of Events

| Step | Actor  | Description                                         |
|------|--------|-----------------------------------------------------|
| 1    | Client | Sends `SET <key> <value>`                           |
| 2    | Server | Parses the request and checks the options           |
| 3    | Server | Stores the value, replacing any previous one        |
| 4    | Server | Replies `+OK`                                       |

## Alternate Flows

### A1: Wrong number of arguments

* Server replies: `ERR wrong number of arguments for 'set' command`

### A2: Condition not met (`NX` on an existing key, `XX` on a missing one)

* Nothing is stored; server replies with a null bulk string

### A3: `GET` option

* Server replies with the previous value, or null if there was none; `WRONGTYPE` if the key held another type

### A4: Invalid option or expiry

* Server replies `ERR syntax error`, or `ERR invalid expire time in 'set' command`

### A5: Value contains spaces

* Through a Redis client, values are sent as RESP bulk strings and may contain anything, binary data included
* Through telnet (inline commands), quote the value

## Related Functional Requirements

* FR-001: String commands
* FR-006: Key expiry
* FR-010: Error replies
* FR-012: Append-only file

## Related Nonfunctional Requirements

* NFR-001: Performance
* NFR-004: Compatibility (replies match Redis)

## Notes

* Expiry options are one way into [UC-011: Expire Keys](uc_011_expire_keys.md).
* Several keys at once, appends and counters are in [UC-007](uc_007_update_strings_and_counters.md).
