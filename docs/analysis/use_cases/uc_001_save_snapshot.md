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
* Relies on `fork` (POSIX); when the fork fails, the server replies `ERR Can't fork` with the reason

### A3: A background save is already running

* Server replies: `ERR Background save already in progress`

### A4: I/O error

* Server replies `ERR` followed by what failed (for example the file could not be created), and the previous snapshot
  is left in place

### A5: Time of the last snapshot (`LASTSAVE`)

* Server replies with the Unix time, in seconds, of the last successful snapshot, as an integer
* A client polls it after `BGSAVE` to learn when the background save has finished

## Related Functional Requirements

* FR-011: Snapshots
* FR-010: Error replies

## Related Nonfunctional Requirements

* NFR-001: Performance (`SAVE` blocking time)
* NFR-003: Reliability (atomic replacement, recovery from the latest snapshot)
* NFR-005: Availability (`BGSAVE` keeps serving)

## Notes

* `SAVE` blocks every client while it writes, as in Redis; prefer `BGSAVE` on large datasets.
* A snapshot is also written at shutdown ([UC-017](uc_017_stop_server.md), FR-017).
