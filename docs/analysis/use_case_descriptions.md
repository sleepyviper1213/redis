# Use Case: Save Snapshot

## Use Case ID

UC-001

## Description

Client asks the server to persist the in-memory keyspace to disk, with `SAVE` (blocking) or `BGSAVE` (in the
background).

## Primary Actor

Client (redis-cli or any Redis client)

## Preconditions

* Server is running
* Command syntax is valid: `SAVE` or `BGSAVE`, with no arguments

## Postconditions

* The snapshot file (`--dump`, default `dump.snap`) holds every live key, in this project's snapshot format
* The previous snapshot was replaced atomically: readers see the old file or the new one, never a partial one
* `LASTSAVE` reports the time of the snapshot

## Flow of Events

| Step | Actor  | Description                                                                                      |
|------|--------|--------------------------------------------------------------------------------------------------|
| 1    | Client | Sends `SAVE`                                                                                     |
| 2    | Server | Checks the command and its arity                                                                 |
| 3    | Server | Writes every live key to a temporary file in the same directory and syncs it to disk             |
| 4    | Server | Renames the temporary file over the snapshot, then syncs the directory                           |
| 5    | Server | Replies `+OK`                                                                                    |

## Alternate Flows

### A1: Wrong number of arguments

* Server replies: `ERR wrong number of arguments for 'save' command`

### A2: Background save (`BGSAVE`)

* Server forks a child that writes the snapshot, and replies `+Background saving started` at once
* The server keeps serving clients; the child's copy-on-write view is the keyspace at the moment of the fork
* The housekeeping task collects the child when it ends and logs the outcome
* Relies on `fork` (POSIX)

### A3: A background save is already running

* Server replies: `ERR Background save already in progress`

### A4: I/O error

* Server replies `ERR` followed by what failed (for example the file could not be created), and the previous snapshot
  is left in place

## Related Functional Requirements

* FR-011: Snapshots
* FR-010: Error replies

## Related Nonfunctional Requirements

* NFR-001: Performance (`SAVE` blocking time)
* NFR-003: Reliability (atomic replacement, recovery from the latest snapshot)
* NFR-005: Availability (`BGSAVE` keeps serving)

## Notes

* `SAVE` blocks every client while it writes, as in Redis; prefer `BGSAVE` on large datasets.
* A snapshot is also written at shutdown (FR-017).

---

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

---

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

* Lists, hashes and sets have their own read commands (`LRANGE`, `HGET`, `SMEMBERS` and others; see FR-002 to FR-004).

---

# Use Case: Delete Key(s)

## Use Case ID

UC-004

## Description

Client deletes one or more keys, of any type, with `DEL`.

## Primary Actor

Client (redis-cli or any Redis client)

## Preconditions

* Server is running
* Command syntax is valid: `DEL <key> [<key> ...]`

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

## Related Functional Requirements

* FR-005: Keyspace commands
* FR-010: Error replies

## Related Nonfunctional Requirements

* NFR-001: Performance
* NFR-004: Compatibility

## Notes

* The command is idempotent: deleting a missing key is not an error.

---

# Use Case: Client Connection Ping-Pong

## Use Case ID

UC-005

## Description

Client checks that the server is alive, and that its connection still works, by sending `PING`.

## Primary Actor

Client (redis-cli, a client library's health check, a load balancer probe)

## Preconditions

* Server is running and listening on `--host`:`--port`
* Client is connected, and was admitted (fewer than `--maxclients` clients)

## Postconditions

* Client knows the server answered; nothing changes on the server

## Flow of Events

| Step | Actor  | Description                                   |
|------|--------|-----------------------------------------------|
| 1    | Client | Sends `PING`                                  |
| 2    | Server | Parses the request and checks its arity       |
| 3    | Server | Replies `+PONG`                               |
| 4    | Client | Receives the reply                            |

## Alternate Flows

### A1: PING with a message

* `PING <message>` is answered with the message, as a bulk string

### A2: Too many arguments

* Server replies: `ERR wrong number of arguments for 'ping' command`

### A3: Idle client

* With `--idle-timeout` set, a client that sends nothing for that long is disconnected; a periodic `PING` keeps it
  connected

### A4: Server full

* A connection past `--maxclients` gets `ERR max number of clients reached` and is closed

### A5: Connection lost

* No reply arrives; the client's own timeout detects it and reconnects

## Related Functional Requirements

* FR-007: Server commands
* FR-008: TCP server and protocol
* FR-014: Client limits

## Related Nonfunctional Requirements

* NFR-001: Performance
* NFR-005: Availability

## Notes

* A slow command on the server delays every reply, `PING` included, until it ends; the command watchdog logs such
  commands (FR-016).
