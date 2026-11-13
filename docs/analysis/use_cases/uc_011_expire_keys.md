# Use Case: Expire Keys

## Use Case ID

UC-011

## Description

Client gives a key a deadline after which it disappears, reads how long it has left, or removes the deadline. A
deadline is set with `EXPIRE`, `PEXPIRE`, `EXPIREAT`, `PEXPIREAT`, or with the expiry options of `SET`
([UC-002](uc_002_set_key_value.md)); it is read with `TTL` and `PTTL`, and removed with `PERSIST`.

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
* An amount whose deadline would overflow gets `ERR invalid expire time`

### A6: Replacing the value

* `SET` without `KEEPTTL` and `MSET` clear the deadline; commands that change a value in place (`INCR`, `APPEND`,
  `LPUSH`, `HSET`, ...) keep it

## Related Functional Requirements

* FR-005: Keyspace commands
* FR-006: Key expiry
* FR-010: Error replies
* FR-012: Append-only file

## Related Nonfunctional Requirements

* NFR-001: Performance (the sweep is bounded)
* NFR-004: Compatibility

## Notes

* The sweep runs every 100 ms and removes at most 1000 expired keys, oldest deadline first, so a key may stay in memory
  a little past its deadline, but no command ever sees it.
* Expired keys are counted in `INFO stats` (`expired_keys`), and keys with a deadline in `INFO keyspace` (`expires`).
