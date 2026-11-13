# Use Case: Inspect the Keyspace

## Use Case ID

UC-012

## Description

Client finds out what the keyspace holds without changing it: whether keys exist (`EXISTS`), which keys match a
pattern (`KEYS`), what type a key holds (`TYPE`), and how many keys there are (`DBSIZE`).

## Primary Actor

Client (redis-cli, a debugging session, an administration script)

## Preconditions

* Server is running
* Command syntax is valid: `EXISTS <key> [<key> ...]`, `KEYS <pattern>`, `TYPE <key>`, `DBSIZE`

## Postconditions

* The keyspace is unchanged, except that keys found expired are removed

## Flow of Events

| Step | Actor  | Description                                                                         |
|------|--------|-------------------------------------------------------------------------------------|
| 1    | Client | Sends `KEYS user:*`                                                                 |
| 2    | Server | Parses the request and checks its arity                                             |
| 3    | Server | Walks every live key and keeps those that match the glob pattern                    |
| 4    | Server | Replies with the matching keys as an array of bulk strings, in no defined order     |

## Alternate Flows

### A1: Check existence (`EXISTS`)

* Server replies with how many of the named keys exist; a key named twice is counted twice

### A2: Type of a key (`TYPE`)

* Server replies, as a simple string, `string`, `list`, `hash` or `set`, or `none` for a missing key

### A3: Number of keys (`DBSIZE`)

* Server replies with the number of keys as an integer

### A4: No key matches

* `KEYS` replies with an empty array

### A5: Wrong number of arguments

* Server replies `ERR wrong number of arguments for '<command>' command`

## Related Functional Requirements

* FR-005: Keyspace commands
* FR-006: Key expiry
* FR-010: Error replies

## Related Nonfunctional Requirements

* NFR-001: Performance
* NFR-004: Compatibility

## Notes

* Patterns use Redis' glob syntax: `*`, `?`, `[abc]`, `[^a]`, `[a-z]`, and `\` to escape.
* `KEYS` visits every key and blocks every other client meanwhile, as in Redis; avoid it on large keyspaces. The
  command watchdog reports a slow one ([UC-018](uc_018_watch_logs.md)).
* Key counts, hits and misses are also in `INFO` ([UC-014](uc_014_inspect_server.md)).
