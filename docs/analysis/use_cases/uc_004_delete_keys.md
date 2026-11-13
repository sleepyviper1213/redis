# Use Case: Delete Key(s)

## Use Case ID

UC-004

## Description

Client deletes one or more keys, of any type, with `DEL`, or every key with `FLUSHDB`.

## Primary Actor

Client (redis-cli or any Redis client)

## Preconditions

* Server is running
* Command syntax is valid: `DEL <key> [<key> ...]`, or `FLUSHDB`

## Postconditions

* Every named key that existed is removed, with its deadline
* With the append-only file on, the deletion is logged

## Flow of Events

| Step | Actor  | Description                                         |
|------|--------|-----------------------------------------------------|
| 1    | Client | Sends `DEL <key1> <key2> ...`                       |
| 2    | Server | Parses the request and checks its arity             |
| 3    | Server | Removes each key that exists, skipping missing ones |
| 4    | Server | Replies with the number of keys removed             |

## Alternate Flows

### A1: No key given

* Server replies: `ERR wrong number of arguments for 'del' command`

### A2: Some keys missing

* Missing keys are skipped and not counted

### A3: Delete every key (`FLUSHDB`)

* Server empties the keyspace and replies `+OK`
* Arguments after the name (such as Redis' `ASYNC` or `SYNC`) are accepted and ignored: the keyspace is always
  cleared at once

## Related Functional Requirements

* FR-005: Keyspace commands
* FR-010: Error replies
* FR-012: Append-only file

## Related Nonfunctional Requirements

* NFR-001: Performance
* NFR-004: Compatibility

## Notes

* The command is idempotent: deleting a missing key is not an error.
* A list, hash or set whose last element is removed (`LPOP`, `HDEL`, `SREM`, `SPOP`) is deleted too, as in Redis.
