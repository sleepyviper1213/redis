# Use Case: Update Strings and Counters

## Use Case ID

UC-007

## Description

Client works with several string keys at once (`MSET`, `MGET`), extends or measures a string (`APPEND`, `STRLEN`),
or uses a string as a 64-bit counter (`INCR`, `DECR`, `INCRBY`, `DECRBY`).

## Primary Actor

Client (redis-cli or any Redis client)

## Preconditions

* Server is running
* Command syntax is valid:
  * `MSET <key> <value> [<key> <value> ...]`
  * `MGET <key> [<key> ...]`
  * `APPEND <key> <value>`, `STRLEN <key>`
  * `INCR <key>`, `DECR <key>`, `INCRBY <key> <delta>`, `DECRBY <key> <delta>`

## Postconditions

* `MSET`: every key holds its new value and has no deadline
* `APPEND`: the key holds the old value followed by the new bytes; a missing key is created
* Counters: the key holds the new number, as a decimal string; a missing key counts from 0
* With the append-only file on, every write is logged

## Flow of Events

| Step | Actor  | Description                                                          |
|------|--------|----------------------------------------------------------------------|
| 1    | Client | Sends `INCR <key>`                                                   |
| 2    | Server | Parses the request and checks its arity                              |
| 3    | Server | Reads the value as a signed 64-bit integer, 0 if the key is missing  |
| 4    | Server | Adds 1 and stores the result, keeping any deadline                   |
| 5    | Server | Replies with the new value as an integer                             |

## Alternate Flows

### A1: Value is not an integer

* Server replies `ERR value is not an integer or out of range`; the value is unchanged
* The same reply for a `<delta>` that is not an integer

### A2: Overflow

* A result outside the signed 64-bit range is refused with `ERR increment or decrement would overflow`

### A3: Key holds another type

* `INCR`, `DECR`, `INCRBY`, `DECRBY`, `APPEND` and `STRLEN` reply
  `WRONGTYPE Operation against a key holding the wrong kind of value`

### A4: Several keys (`MSET`, `MGET`)

* `MSET` stores every pair, replacing values of any type, and replies `+OK`; an odd number of arguments after the name
  gets `ERR wrong number of arguments for 'mset' command`
* `MGET` replies with an array holding each value in order, null for a key that is missing or not a string

### A5: Append and length (`APPEND`, `STRLEN`)

* `APPEND` replies with the length of the string after the append
* `STRLEN` replies with the length in bytes, 0 for a missing key

### A6: Wrong number of arguments

* Server replies `ERR wrong number of arguments for '<command>' command`

## Related Functional Requirements

* FR-001: String commands
* FR-010: Error replies
* FR-012: Append-only file

## Related Nonfunctional Requirements

* NFR-001: Performance
* NFR-004: Compatibility

## Notes

* Commands run one at a time, so `INCR` is atomic: two clients incrementing the same key never lose an update.
* Single-key `SET` and `GET` are [UC-002](uc_002_set_key_value.md) and [UC-003](uc_003_get_key_value.md).
