# Use Case: Stop the Server

## Use Case ID

UC-017

## Description

Operator stops the server with `SIGINT` (Ctrl+C) or `SIGTERM`. The server stops accepting clients, finishes the work in
progress, saves a final snapshot and exits, so that no acknowledged write is lost.

## Primary Actor

Operator (a person at a shell, a service manager, a container runtime)

## Preconditions

* Server is running

## Postconditions

* The snapshot file holds the keyspace as it was after the last command executed
* With the append-only file on, it is flushed and closed
* The process exits with status 0, or non-zero if the final save failed

## Flow of Events

| Step | Actor    | Description                                                                                 |
|------|----------|---------------------------------------------------------------------------------------------|
| 1    | Operator | Sends `SIGTERM`, or presses Ctrl+C                                                          |
| 2    | Server   | Logs `Received SIGTERM, shutting down`                                                      |
| 3    | Server   | Stops accepting connections and the housekeeping task, and drains the client sessions       |
| 4    | Server   | Waits for a batch of commands that is still executing                                       |
| 5    | Server   | Cancels a running `BGSAVE` and an unfinished `BGREWRITEAOF`                                 |
| 6    | Server   | Flushes and closes the append-only file                                                     |
| 7    | Server   | Saves a final snapshot to `--dump`, atomically                                              |
| 8    | Server   | Logs `Redis is now ready to exit, bye bye...` and exits with status 0                        |

## Alternate Flows

### A1: Final save fails

* Server logs `final snapshot failed: ` and the reason at critical level, and exits non-zero, so a supervisor sees the
  failure; the previous snapshot is left in place
* Redis keeps serving in this case; this server cannot resume once its event loop has stopped

### A2: A server thread fails

* An I/O thread, the accept loop or the housekeeping task that fails ends the server the same way, from step 3; the
  failure is logged and the exit status is non-zero

### A3: Process killed (`SIGKILL`, power loss)

* No final save happens; at the next start the server recovers from the last snapshot, or, with `--appendonly`, from
  the append-only file, which loses at most the writes not yet synced under the `--appendfsync` policy

## Related Functional Requirements

* FR-017: Graceful shutdown
* FR-011: Snapshots
* FR-012: Append-only file

## Related Nonfunctional Requirements

* NFR-003: Reliability
* NFR-005: Availability

## Notes

* Clients connected at shutdown lose their connection; replies not yet written to them are dropped.
* The sequence is drawn in `sequence_threads_request_and_shutdown.puml`.
