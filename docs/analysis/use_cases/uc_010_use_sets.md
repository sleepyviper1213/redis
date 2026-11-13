# Use Case: Store and Read Sets

## Use Case ID

UC-010

## Description

Client keeps an unordered collection of unique strings under one key: it adds and removes members (`SADD`, `SREM`),
tests and lists them (`SISMEMBER`, `SMEMBERS`, `SCARD`), intersects sets (`SINTER`), and takes a random member
(`SPOP`).

## Primary Actor

Client (redis-cli or any Redis client)

## Preconditions

* Server is running
* Command syntax is valid:
  * `SADD <key> <member> [<member> ...]`, `SREM <key> <member> [<member> ...]`
  * `SISMEMBER <key> <member>`, `SMEMBERS <key>`, `SCARD <key>`
  * `SINTER <key> [<key> ...]`, `SPOP <key> [<count>]`

## Postconditions

* Added members are in the set once each; a missing key is created as a set
* A set whose last member is removed is deleted
* With the append-only file on, every write is logged; `SPOP` is logged as `SREM` of the members it took, so a replay
  removes the same ones

## Flow of Events

| Step | Actor  | Description                                                       |
|------|--------|-------------------------------------------------------------------|
| 1    | Client | Sends `SADD tags red green red`                                   |
| 2    | Server | Parses the request and checks its arity                           |
| 3    | Server | Creates the set if it is missing, adds each member                |
| 4    | Server | Replies with the number of members that were new, here `2`        |
| 5    | Client | Sends `SISMEMBER tags red`                                        |
| 6    | Server | Replies `1` (member) or `0` (not a member, or no such key)        |

## Alternate Flows

### A1: Remove members (`SREM`)

* Server replies with the number of members removed; missing members are not counted

### A2: Intersection (`SINTER`)

* Server replies with the members found in every set named; a missing key counts as an empty set, so the reply is
  empty
* Every key is type-checked, even after a missing one

### A3: Random member (`SPOP`)

* Without a count: one member, removed, or null for a missing key
* With a count: an array of up to `count` members, removed; an empty array for a missing key
* A count that is negative or not an integer gets `ERR value is out of range, must be positive`

### A4: Key holds another type

* Server replies `WRONGTYPE Operation against a key holding the wrong kind of value`

### A5: Wrong number of arguments

* Server replies `ERR wrong number of arguments for '<command>' command`

## Related Functional Requirements

* FR-004: Set commands
* FR-010: Error replies
* FR-012: Append-only file

## Related Nonfunctional Requirements

* NFR-001: Performance
* NFR-004: Compatibility

## Notes

* `SMEMBERS` and `SINTER` reply in no defined order.
