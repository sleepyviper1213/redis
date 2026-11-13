# Use Case: Store and Read Lists

## Use Case ID

UC-008

## Description

Client keeps an ordered sequence of strings under one key, as a queue or a stack: it pushes at either end
(`LPUSH`, `RPUSH`), pops from either end (`LPOP`, `RPOP`), and reads it (`LLEN`, `LRANGE`, `LINDEX`).

## Primary Actor

Client (redis-cli or any Redis client)

## Preconditions

* Server is running
* Command syntax is valid:
  * `LPUSH <key> <element> [<element> ...]`, `RPUSH <key> <element> [<element> ...]`
  * `LPOP <key> [<count>]`, `RPOP <key> [<count>]`
  * `LLEN <key>`, `LRANGE <key> <start> <stop>`, `LINDEX <key> <index>`

## Postconditions

* Pushed elements are in the list, in the order given; a missing key is created as a list
* A list whose last element is popped is deleted
* With the append-only file on, every write is logged

## Flow of Events

| Step | Actor  | Description                                                     |
|------|--------|-----------------------------------------------------------------|
| 1    | Client | Sends `RPUSH jobs a b c`                                        |
| 2    | Server | Parses the request and checks its arity                         |
| 3    | Server | Creates the list if it is missing, appends each element in turn |
| 4    | Server | Replies with the length of the list, here `3`                   |
| 5    | Client | Sends `LPOP jobs`                                               |
| 6    | Server | Removes the first element and replies with it, here `a`         |

## Alternate Flows

### A1: Pop with a count

* `LPOP <key> <count>` replies with an array of up to `count` elements
* A count that is negative or not an integer gets `ERR value is out of range, must be positive`

### A2: Pop from a missing key

* Without a count: a null bulk string; with a count: a null array

### A3: Read a range (`LRANGE`)

* Indexes start at 0; a negative index counts from the end (`-1` is the last element)
* The range is clamped to the list; an empty range, or a missing key, gives an empty array
* `LINDEX` replies with the element at the index, or null if the index is out of range or the key is missing

### A4: Key holds another type

* Server replies `WRONGTYPE Operation against a key holding the wrong kind of value`

### A5: Index or count is not an integer

* `LRANGE` and `LINDEX` reply `ERR value is not an integer or out of range`

### A6: Wrong number of arguments

* Server replies `ERR wrong number of arguments for '<command>' command`

## Related Functional Requirements

* FR-002: List commands
* FR-010: Error replies
* FR-012: Append-only file

## Related Nonfunctional Requirements

* NFR-001: Performance
* NFR-004: Compatibility

## Notes

* `LPUSH a b c` inserts each element at the head in turn, so the list reads `c b a`, as in Redis.
* Blocking pops (`BLPOP`) are not supported.
