# Use Case: Get Key Value

## Use Case ID

UC-003

## Description

Client reads the string value of a key with `GET`.

## Primary Actor

Client (redis-cli or any Redis client)

## Preconditions

* Server is running
* Command syntax is valid: `GET <key>`

## Postconditions

* The keyspace is unchanged, except that a key found expired is removed

## Flow of Events

| Step | Actor  | Description                                   |
|------|--------|-----------------------------------------------|
| 1    | Client | Sends `GET <key>`                             |
| 2    | Server | Parses the request and checks its arity       |
| 3    | Server | Looks the key up as a string                  |
| 4    | Server | Replies with the value as a bulk string       |

## Alternate Flows

### A1: Key missing or expired

* An expired key is removed on access; server replies with a null bulk string

### A2: Key holds another type

* Server replies: `WRONGTYPE Operation against a key holding the wrong kind of value`

### A3: Wrong number of arguments

* Server replies: `ERR wrong number of arguments for 'get' command`

## Related Functional Requirements

* FR-001: String commands
* FR-006: Key expiry
* FR-010: Error replies

## Related Nonfunctional Requirements

* NFR-001: Performance
* NFR-004: Compatibility

## Notes

* Lists, hashes and sets have their own read commands: see [UC-008](uc_008_use_lists.md),
  [UC-009](uc_009_use_hashes.md) and [UC-010](uc_010_use_sets.md).
* `MGET` reads several keys at once ([UC-007](uc_007_update_strings_and_counters.md)).
