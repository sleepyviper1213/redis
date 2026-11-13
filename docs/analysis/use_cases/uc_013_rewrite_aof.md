# Use Case: Rewrite the Append-Only File

## Use Case ID

UC-013

## Description

Client asks the server to compact the append-only file with `BGREWRITEAOF`. A forked child writes the current
keyspace as a new base snapshot while the server keeps logging writes to a fresh incremental file; when the child
succeeds, the manifest is switched to the new base and the old files are removed.

## Primary Actor

Client (redis-cli, an operator's maintenance script)

## Preconditions

* Server is running with `--appendonly`
* No rewrite is running
* Command syntax is valid: `BGREWRITEAOF`, with no arguments

## Postconditions

* The manifest in `--appenddir` lists the new base file and one incremental file
* The old base and incremental files are deleted
* Replaying the files at the next startup gives the same keyspace as before the rewrite, plus the writes made since

## Flow of Events

| Step | Actor  | Description                                                                                      |
|------|--------|--------------------------------------------------------------------------------------------------|
| 1    | Client | Sends `BGREWRITEAOF`                                                                             |
| 2    | Server | Flushes pending log writes to disk                                                               |
| 3    | Server | Creates a new, empty incremental file and records it in the manifest; later writes go there      |
| 4    | Server | Forks a child that writes the keyspace to a new base file                                        |
| 5    | Server | Replies `+Background append only file rewriting started` and keeps serving clients               |
| 6    | Server | The housekeeping task collects the child when it ends                                            |
| 7    | Server | Writes a manifest that lists only the new base and the newest incremental file                   |
| 8    | Server | Deletes the old files and logs `Background AOF rewrite finished successfully`                    |

## Alternate Flows

### A1: Append-only file disabled

* Server replies: `ERR Append only file is not enabled`

### A2: A rewrite is already running

* Server replies: `ERR Background append only file rewriting already in progress`

### A3: Fork fails

* Server replies `ERR Can't fork` with the reason, and logs it
* Harmless: the manifest lists both incremental files, which replay as one; the next rewrite merges them

### A4: The child fails, or the manifest cannot be written

* The failure is logged at error level; the old files stay in use and remain complete

### A5: Shutdown during a rewrite

* The child is killed and the unfinished rewrite dropped; the files it would have replaced are still whole

### A6: I/O error before the fork

* Server replies `ERR` followed by what failed (for example the incremental file could not be created)

## Related Functional Requirements

* FR-012: Append-only file
* FR-010: Error replies

## Related Nonfunctional Requirements

* NFR-003: Reliability (no step leaves the files unreadable)
* NFR-005: Availability (the server keeps serving during the rewrite)
* NFR-007: Portability (relies on `fork`, POSIX)

## Notes

* Progress is visible in `INFO persistence` (`aof_rewrite_in_progress`).
* Every write command (UC-002, UC-004, UC-007 to UC-011) is logged to the incremental file before its reply is sent;
  with `--appendfsync always`, it is also on disk by then.
