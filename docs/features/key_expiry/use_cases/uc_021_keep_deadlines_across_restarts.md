# Use Case: Keep Deadlines Across Restarts

## Use Case ID

UC-021

## Description

A key's deadline survives a restart, through the snapshot and the append-only file, so a key lives neither longer nor
shorter than it was told to. Deadlines are set in [UC-011](uc_011_expire_keys.md); the files are written by
[UC-001](../../persistence/use_cases/uc_001_save_snapshot.md) and [UC-013](../../persistence/use_cases/uc_013_rewrite_aof.md).

## Primary Actor

Operator (restarts the server and relies on the files); the Client's commands produce what is persisted.

## Preconditions

* Server is running with a snapshot (`--dump`) or with `--appendonly`
* Keys with deadlines exist

## Postconditions

* After a restart, each key has the deadline it had, as an absolute time
* A key whose deadline passed while the server was down is not restored from a snapshot
* A replay of the append-only file does not extend any lifetime

## Flow of Events

### Main flow: restart from a snapshot

| Step | Actor    | Description                                                                         |
|------|----------|-------------------------------------------------------------------------------------|
| 1    | Server   | On `SAVE`, `BGSAVE` or shutdown, writes each key with its absolute deadline         |
| 2    | Operator | Restarts the server                                                                 |
| 3    | Server   | Loads the snapshot, skipping keys whose deadline has passed                         |
| 4    | Server   | Serves the remaining keys with their original deadlines                             |

## Alternate Flows

### A1: Append-only file on

* `EXPIRE` and `PEXPIRE` are logged as the absolute `PEXPIREAT`; `SET` with an expiry as `SET ... PXAT`
* A deadline already past, set by the `EXPIRE` family, is logged as `DEL`; `SET` with a past `PXAT` is logged as it was
  received, and the `DEL` follows when the key is read or swept
* A key that expired, by access or by the sweep, is logged as `DEL` ([UC-020](uc_020_remove_expired_keys.md))

### A2: Replay at startup

* Expiry is off while the log is replayed, so the result does not depend on when the replay runs
* Expiry resumes when the replay ends

### A3: Base of the append-only file

* A base, written when the file starts or is rewritten, holds only live keys, each with its absolute deadline
* At startup the base is loaded keeping deadlines that have passed since it was written, unlike a plain snapshot
  (main flow, step 3), so the logged commands that follow still find those keys; the logged `DEL` for a key that
  expired removes it, and expiry resumes after the replay (A2)

## Related Functional Requirements

* FR-006: Key expiry (FR-006.10)
* FR-011: Snapshots
* FR-012: Append-only file

## Related Nonfunctional Requirements

* NFR-003: Reliability
* NFR-010: Key expiry (NFR-010.5 durability)

## Acceptance Criteria and Diagrams

### AC-09: Deadlines survive a restart

Requirements: FR-006.10, FR-011, FR-012, NFR-003, NFR-010.5

* Given a snapshot (`SAVE`, `BGSAVE`, shutdown), then each key's absolute deadline is saved with it; on load, keys
  whose deadline passed while the server was down are not restored.
* Given `--appendonly`, then `EXPIRE` and `PEXPIRE` are logged as the absolute `PEXPIREAT`, and `SET` with an expiry as
  `SET ... PXAT`, so a replay does not extend the lifetime.
* Given a deadline already past set by the `EXPIRE` family, then the log records a `DEL`; given `SET` with a past
  `PXAT`, then the log records the `SET`, and a `DEL` once the key is read or swept.
* Given a key that expired, by access or by the sweep, then a `DEL` is logged, so the replay removes it too.
* Given a replay of the log, then nothing expires while it runs; expiry resumes after.

Diagram: [sequence_key_expiry.puml](../diagrams/sequence_key_expiry.puml)

## Notes

* Deadlines use the wall clock, so a clock set backwards extends lifetimes and one set forwards shortens them.
