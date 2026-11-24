# Persistence: Test Scenarios

Test scenarios for snapshots and the append-only file, derived from the flows of
[UC-001](use_cases/uc_001_save_snapshot.md) and [UC-013](use_cases/uc_013_rewrite_aof.md) (FR-011, FR-012, NFR-003).
They extend the [Master Test Plan](../../test/test_plan_and_report.md), whose section 12 lists FR-011 and FR-012 as
covered at the unit level only.

Scenario IDs are `TC-nnn`, one sequence for the whole project ([ID ranges](../README.md#id-ranges)). These use cases have no AC-xxx sections, so scenarios come from the flow of events and
the alternate flows. Replies are quoted as the server writes them (RESP2 notation).

## How to read the coverage columns

Two columns, one question each:

| Column | Meaning |
|--------|---------|
| *Level* | The levels the scenario is designed for. A plan, not a record of what runs |
| *Tested at* | The levels where a test exists today, found from the *Existing test* column: `Unit`, `Integration`, `Stress`, `Differential`, or `none` |
| *Status* | `Covered` when every level in *Level* is in *Tested at*; `Partial` when some are; `Gap` when none are |

So a row with Level `Unit, Integration`, Tested at `Unit` and Status `Partial` has its unit test and lacks its
integration test. `Differential` counts as tested only where the differential script sends the scenario and the run is
gating.

## Levels and technique

| Level        | Use for                                                                                           |
|--------------|---------------------------------------------------------------------------------------------------|
| Unit         | Snapshot, log and file-replacement logic against temporary directories, no socket                  |
| Integration  | A running server through hiredis: `SAVE`, `BGSAVE`, restart over the same files                    |
| Stress       | Commands and saves at the same time, under ThreadSanitizer                                        |
| Differential | Not used: the snapshot and log formats are this project's own, not Redis's                         |

Tests of a forked child poll with a timeout until the child is collected, never one fixed sleep.

## 1. Saving a snapshot (UC-001 main flow, A1, A4, A5)

| ID | Behaviour | Given / When | Expected | Level | Existing test | Tested at | Status |
|-----------|-----------------------------------|-----------------------------------------------------------|--------------------------------------------------------------------|--------------------|------------------------------------------------------------|----------|----------|
| TC-124 | `SAVE` writes the snapshot | A keyspace with data; `SAVE` | `+OK`; the `--dump` file exists | Unit, Integration | `snapshot`: "Saving through commands" | Unit | Partial |
| TC-125 | `SAVE` of an empty keyspace, and over an earlier snapshot | `FLUSHALL`, `SAVE`; then `SAVE` again | `+OK` each time; the file exists and is the newest | Integration | Integration `TC-002`, `TC-003` (hidden, A-5) | Integration | Covered |
| TC-126 | Wrong arity | `SAVE x`; `BGSAVE x`; `LASTSAVE x`; `BGREWRITEAOF x` | `-ERR wrong number of arguments for '...' command` | Unit | None found | none | Gap |
| TC-127 | `LASTSAVE` reports the save time | Note `LASTSAVE`; wait over a second; `SAVE`; `LASTSAVE` | An integer in seconds that advanced | Unit | `snapshot`: "Saving through commands" | Unit | Covered |
| TC-128 | The file is replaced as a whole | An old snapshot; a new one written but not yet committed; then committed, or dropped | Until commit the target is the old file; after commit the new one and no temporary; a dropped save leaves the old file | Unit | `save_file`: "A file is replaced as a whole", "A file is created if there was none", "More than the buffer holds is written whole" | Unit | Covered |
| TC-129 | A stale or planted temporary is harmless | A temporary left by a crash; a link planted at its name | The stale file is replaced; the link is not followed | Unit | `save_file`: "A leftover under the temporary's name is replaced", "The temporary is not made through a link" | Unit | Covered |
| TC-130 | I/O error leaves the old snapshot | The directory cannot be written, or the temporary cannot be created; `SAVE` | `-ERR` with what failed; the previous snapshot is untouched | Unit, Integration | `save_file`: "A save that cannot start says which step failed"; `persist_error` (wording); Integration `TC-005` (hidden, A-5) | Integration | Partial |
| TC-131 | Every value type round-trips | Keys of each type, binary keys and values; save, load into a fresh server | The same keys and values, byte for byte | Unit | `snapshot`: "Snapshots round-trip every value type" | Unit | Covered |
| TC-132 | A deadline is stored as absolute | A key with `EXPIRE 100`; save; load | `TTL` is 99 or 100 after load; keys without a deadline stay `-1` | Unit | `snapshot`: "Snapshots round-trip every value type" | Unit | Covered |
| TC-133 | A key past its deadline is not restored | A short deadline passes after the save; load | `EXISTS` is `:0` for it, `:1` for a live key | Unit | `snapshot`: "Snapshots round-trip every value type" | Unit | Covered |
| TC-134 | The outcome is logged | A successful save; a failed save | One log line each, with the reason on failure | Integration | None found | none | Gap |

## 2. Background save (UC-001 A2, A3)

| ID | Behaviour | Given / When | Expected | Level | Existing test | Tested at | Status |
|-----------|-----------------------------------|-----------------------------------------------------------|--------------------------------------------------------------------|--------------------|------------------------------------------------------------|----------|----------|
| TC-135 | `BGSAVE` saves in a child | `BGSAVE`; wait until the child is collected | `+Background saving started`; the snapshot loads and holds the key | Unit | `snapshot`: "Saving in the background" (`[fork]`) | Unit | Covered |
| TC-136 | A second `BGSAVE` while one runs | `BGSAVE` twice in a row | The second gets `-ERR Background save already in progress` | Unit | `snapshot`: "Saving in the background" (`[fork]`) | Unit | Covered |
| TC-137 | The child sees the keyspace at the fork | `BGSAVE`, then `SET` a new key before the child ends | The snapshot holds the keyspace at the fork, not the later write | Unit | None found | none | Gap |
| TC-138 | Fork fails | `fork` returns an error; `BGSAVE` | `-ERR Can't fork: <reason>`; the server keeps serving | Unit | `persist_error` (wording only) | Unit | Covered |
| TC-139 | The server keeps serving during a save | Clients send commands while `BGSAVE` runs, and `SAVE` runs from two clients | Replies stay correct; no race reported | Stress, Integration | Integration `TC-006` (hidden, A-5; checks `SAVE` against `SAVE`) | Integration | Partial |

## 3. Loading a snapshot (UC-016 A2, FR-018)

The start-up behaviour that follows from a failed load is in [server_lifecycle](../server_lifecycle/test_plan.md)
(TC-236).

| ID | Behaviour | Given / When | Expected | Level | Existing test | Tested at | Status |
|-----------|-----------------------------------|-----------------------------------------------------------|--------------------------------------------------------------------|--------------------|------------------------------------------------------------|----------|----------|
| TC-140 | A missing file is an empty start | The snapshot file does not exist; load | Success; the keyspace is left alone | Unit | `snapshot`: "Loading rejects files that are not valid snapshots" | Unit | Covered |
| TC-141 | Invalid files are rejected whole | A file that is not a snapshot; a truncated one; a version 1 file; a record that is not valid CBOR | Load fails with its own kind (version, corrupt); nothing partial is loaded | Unit | `snapshot`: "Loading rejects files that are not valid snapshots" | Unit | Covered |

A checksum of the snapshot is deferred (risk R-001 in the SRS); add a scenario for a flipped byte when it exists.

## 4. The append-only file (FR-012)

| ID | Behaviour | Given / When | Expected | Level | Existing test | Tested at | Status |
|-----------|-----------------------------------|-----------------------------------------------------------|--------------------------------------------------------------------|--------------------|------------------------------------------------------------|----------|----------|
| TC-142 | Only successful writes are logged | `SET`, `GET`, `INCR`, a `LPUSH` that fails, `DEL` | The log holds `SET`, `INCR`, `DEL`, in order; no reads, no failure | Unit | `aof`: "Write commands are logged" | Unit | Covered |
| TC-143 | Commands that cannot be replayed as received are rewritten | `SET ... EX`, `EXPIRE`, `EXPIRE` into the past, `SPOP` | The log holds absolute forms (`PXAT`, `PEXPIREAT`), `DEL`, and what `SPOP` removed | Unit | `aof`: "Commands that cannot be replayed as received" | Unit | Covered |
| TC-144 | A restart restores every value type | Log of every kind of value; start another server over the files | Same keyspace; the original deadline; the new server keeps appending after the old commands | Unit | `aof`: "A restart gets back what was logged" | Unit | Covered |
| TC-145 | Replay does not depend on the clock | Keys whose deadlines passed before the restart | Replay applies every logged command; expiry resumes afterwards | Unit | `aof`: "A replay does not depend on when it runs", "A keyspace that tracks its expiry" | Unit | Covered |
| TC-146 | A torn last command is cut off | The log ends in the middle of a command; start | The whole commands are kept; the file is truncated to them | Unit | `aof`: "A log that ends in the middle of a command" | Unit | Covered |
| TC-147 | An unparseable command is refused | The log holds `*x`; start | Start fails | Unit | `aof`: "A log that ends in the middle of a command" | Unit | Covered |
| TC-148 | Turning the log on keeps the snapshot | A snapshot from a server without the log; start with `--appendonly` | The snapshot's keys are present and survive a restart without the snapshot | Unit | `aof`: "Turning the append-only file on keeps the snapshot" | Unit | Covered |
| TC-149 | `--appendfsync` policies | `always`, `everysec`, `no`; write, then inspect when the data is on disk | `always`: on disk before the reply; the others: within their bounds | Unit | None found (the test server takes a policy, no scenario targets one) | none | Gap |
| TC-150 | Restart of a running server with the log on | Start `--appendonly`; write; stop; start again over the same directory | The writes are there | Integration | None found (A-7) | none | Gap |
| TC-297 | The `everysec` sync thread starts, and `close()` and the destructor stop it | `AppendOnlyFile` with `everysec`, loaded; several writes; then `close()`, and separately destruction with no `close()` | `close()` returns at once, without waiting out the one-second period; every write is replayed after a restart; destruction without `close()` ends the thread, with no hang and no sanitizer report. That the thread is running after `load()` cannot be observed from outside (no accessor) and is not tested | Unit | None found (every `aof` test runs with `FsyncPolicy::no`, so the thread is never started; TC-149, TC-261) | none | Gap |

## 5. Rewriting the log (UC-013)

| ID | Behaviour | Given / When | Expected | Level | Existing test | Tested at | Status |
|-----------|-----------------------------------|-----------------------------------------------------------|--------------------------------------------------------------------|--------------------|------------------------------------------------------------|----------|----------|
| TC-151 | `BGREWRITEAOF` compacts the log | A long log; start a rewrite; write during it; wait | One base and one short incremental file; a restart over them loses nothing | Unit | `aof`: "Rewriting the log" (`[fork]`) | Unit | Covered |
| TC-152 | Refusals | The log is off; a rewrite is already running | `-ERR Append only file is not enabled`; `-ERR Background append only file rewriting already in progress` | Unit, Integration | None found (`persist_error` words the second) | none | Gap |
| TC-153 | Fork fails | `fork` returns an error; `BGREWRITEAOF` | `-ERR Can't fork: <reason>`; both incremental files replay as one | Unit | `persist_error` (wording only) | Unit | Covered |
| TC-154 | A failed rewrite keeps the old files | The child fails; or the manifest cannot be written; or the incremental file cannot be created | Error logged (or `-ERR` before the fork); the old files remain complete and replay | Unit | None found | none | Gap |
| TC-155 | Shutdown during a rewrite | A rewrite is running; the server stops | The child is killed; the files it would have replaced are whole | Integration | None found | none | Gap |
| TC-156 | Progress is visible | During a rewrite; `INFO persistence` | `aof_rewrite_in_progress:1`, then `0` | Integration | None found | none | Gap |

## 6. Files that cannot be trusted (NFR-003, FR-018)

| ID | Behaviour | Given / When | Expected | Level | Existing test | Tested at | Status |
|-----------|-----------------------------------|-----------------------------------------------------------|--------------------------------------------------------------------|--------------------|------------------------------------------------------------|----------|----------|
| TC-157 | A manifest cannot point outside the directory | A name that climbs out (`../x`), a path, or a name that is not the server's own | Start is refused with the offending name; the other file is untouched; the server's own names start | Unit | `aof`: "A manifest cannot point outside the append-only directory" | Unit | Covered |
| TC-158 | One server per directory | A second server starts over a directory in use; then the first closes | The second is refused and the first carries on; after close the directory is free | Unit | `aof`: "One server at a time uses the directory" | Unit | Covered |
| TC-159 | Files are private and links are not followed | A link planted where the manifest or log is written | The file behind the link is untouched; the server's files are owner-only | Unit | `aof`: "The server's files are private, and links are not followed" | Unit | Covered |
| TC-261 | `everysec` data-loss bound | Writes under `--appendfsync everysec`; `SIGKILL` after a pause of more than one second; restart | Every write older than one second is present; the writes lost are all within the last second. Under `always`, every acknowledged write is present | Integration | `appendfsync_bound`: "TC-261" (POSIX only, runs in `scripts/test_server.sh`) | none | Gap |

## Entry and exit criteria

* **Entry**: the server builds; the unit suite runs in the chosen preset; for `[fork]` scenarios, a Linux or macOS
  preset.
* **Exit**: every scenario is Unit or higher, or has a Gap recorded in the master plan's anomaly list (section 16;
  A-5 and A-7 apply here). The Integration scenarios pass on Linux and macOS in CI.
