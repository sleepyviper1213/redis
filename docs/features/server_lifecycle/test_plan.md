# Server Lifecycle: Test Scenarios

Test scenarios for configuring, starting and stopping the server, derived from the flows of
[UC-016](use_cases/uc_016_configure_and_start.md) and [UC-017](use_cases/uc_017_stop_server.md) (FR-013, FR-017,
FR-018; NFR-003). They extend the [Master Test Plan](../../test/test_plan_and_report.md), whose section 12 lists
FR-013 as a gap (anomaly A-2) and FR-017 as a gap (anomaly A-6).

Scenario IDs are `TC-nnn`, one sequence for the whole project ([ID ranges](../README.md#id-ranges)). These use cases have no AC-xxx sections, so scenarios come from the flow of events and
the alternate flows. The startup sequence is drawn in [activity_startup.puml](diagrams/activity_startup.puml).

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

| Level       | Use for                                                                                              |
|-------------|------------------------------------------------------------------------------------------------------|
| Unit        | Components in process: the loaders, the listener, the logger                                           |
| Integration | The real binary started as a process: options, exit status, log lines, signals, files after exit      |
| Stress      | The stress run starts and stops the server; its stop must be clean                                    |

Almost every scenario here needs the binary run as a child process, which is the integration level and is what A-2 and
A-6 recommend. Signals cannot be sent to a native Windows process from bash, so the signal scenarios run on Linux and
macOS only. Wait for `Ready to accept connections` or a connection, and for the process to exit, with a timeout; never
one fixed sleep.

## 1. Configuration (UC-016 steps 1, 2; A1; FR-013)

| ID | Behaviour | Given / When | Expected | Level | Existing test | Tested at | Status |
|------------|-----------------------------------|-----------------------------------------------------------|--------------------------------------------------------------------|-------------|------------------------------------------------------------|----------|----------|
| TC-227 | Options on the command line | `--port`, `--host`, `--dump`, `--appendonly`, `--io-threads` given | The server listens and behaves as configured; `CONFIG GET` reports the values | Integration | None found (A-2) | none | Gap |
| TC-228 | Options in a config file | `--config server.toml` holding the same settings | The same behaviour as TC-227 | Integration | None found (A-2) | none | Gap |
| TC-229 | Options in `REDIS_*` variables | `REDIS_PORT` and others set; no options | The same behaviour as TC-227 | Integration | None found (A-2) | none | Gap |
| TC-230 | Precedence among the three sources | The same option in the command line, file and environment with different values | One value wins, in the documented order (the README lists it) | Integration | None found (A-2) | none | Gap |
| TC-231 | Invalid option exits non-zero | An unknown option; `--host` not an IPv4 address; a port that is not positive; `--appendfsync maybe` | The reason on stderr; exit status non-zero; no port bound | Integration | None found (A-2) | none | Gap |
| TC-232 | `--help` and `--version` | Run with each | Options listed by group; the version printed; exit 0 | Integration | None found | none | Gap |

## 2. Starting (UC-016 steps 3 to 7, A2 to A6; FR-018)

| ID | Behaviour | Given / When | Expected | Level | Existing test | Tested at | Status |
|------------|-----------------------------------|-----------------------------------------------------------|--------------------------------------------------------------------|-------------|------------------------------------------------------------|----------|----------|
| TC-233 | A start logs its progress and serves | Start with a port, a dump file and a log file | `Server started, pid ..., port ..., dump file ...`, then `Ready to accept connections`; a client connects | Integration | `scripts/test_server.sh` and `stress_server.sh` start the binary and wait for connections; no test reads the log lines | Stress | Partial |
| TC-234 | Start with no files | No snapshot and no append-only directory | An empty keyspace; the server serves | Unit, Integration | `snapshot`: "Loading rejects files that are not valid snapshots" (missing file loads as empty) | Unit | Partial |
| TC-235 | Start restores the data | A snapshot exists, or with `--appendonly` an append-only directory; start | The keys are present; with the log on, the log wins over the snapshot | Unit, Integration | `snapshot`: "Snapshots round-trip every value type"; `aof`: "A restart gets back what was logged", "Turning the append-only file on keeps the snapshot" | Unit | Partial |
| TC-236 | Unreadable data refuses start | A snapshot that is not valid, truncated or of another version; an append-only file that cannot be parsed; then start | A critical log line with the reason; exit non-zero; the files are not overwritten | Unit, Integration | `snapshot`: "Loading rejects files that are not valid snapshots"; `aof`: "A log that ends in the middle of a command" (the loaders refuse; exit status and critical log are not tested) | Unit | Partial |
| TC-237 | A log cut short by a crash starts | The log ends in the middle of a command; start | Truncated to the last whole command with a warning; the server starts | Unit, Integration | `aof`: "A log that ends in the middle of a command" | Unit | Partial |
| TC-238 | The append-only directory is locked | Another server holds `--appenddir`; start | Fails with `<dir> is in use by another server; refusing to share it`; the first server is undisturbed | Unit, Integration | `aof`: "One server at a time uses the directory" | Unit | Partial |
| TC-239 | Port in use | The port is bound; start | A critical log line with the reason; exit non-zero | Integration | None found | none | Gap |
| TC-240 | Log file cannot be opened | `--log-file` in a directory that does not exist; start | The server logs to stderr only, says so, and starts | Integration | None found | none | Gap |
| TC-295 | A start that fails after its threads began stops them | `--appendonly --appendfsync everysec` over a good directory, and the port bound by another socket; start. The log is replayed and the sync thread runs before `listen()` fails, so only destructors stop what was started | A critical log line with the reason (as TC-239); exit non-zero within a few seconds, no hang; the append-only directory is free again (a second server starts over it); no sanitizer report | Integration | None found | none | Gap |

## 3. Stopping (UC-017 steps 1 to 8; FR-017)

| ID | Behaviour | Given / When | Expected | Level | Existing test | Tested at | Status |
|------------|-----------------------------------|-----------------------------------------------------------|--------------------------------------------------------------------|-------------|------------------------------------------------------------|----------|----------|
| TC-241 | `SIGTERM` stops the server cleanly | A running server; `SIGTERM` | `Received SIGTERM, shutting down`; exit status 0; `Redis is now ready to exit, bye bye...` | Integration | `scripts/common.sh` `stop_server`, used by the test, stress and bench scripts: sends `SIGTERM`, checks exit status 0 and no sanitizer report; the log lines are not checked; skipped on Windows | Stress | Partial |
| TC-242 | `SIGINT` (Ctrl+C) does the same | A running server; `SIGINT` | As TC-241 | Integration | None found | none | Gap |
| TC-243 | The listener stops accepting and drains | A listener with no clients, or with a live client; stop | The accept loop ends without error; the client is hung up on; everything drains | Unit | `listener`: "A listener stops" (`[shutdown]`) | Unit | Covered |
| TC-244 | Stopping twice, and after a stop | Stop twice; then connect | Harmless; new connections are refused | Unit | `listener`: "A listener stops" (`[shutdown]`) | Unit | Covered |
| TC-245 | A running batch is awaited | A batch of commands is executing; `SIGTERM` | The batch finishes and is answered or logged before the save; no write is half applied | Integration | None found | none | Gap |
| TC-246 | Background work is cancelled | A `BGSAVE` and a `BGREWRITEAOF` running; `SIGTERM` | The children are killed; the old files are whole; the server exits | Integration | None found | none | Gap |
| TC-247 | The append-only file is flushed and closed | `--appendonly`, `--appendfsync no`; writes; `SIGTERM`; restart | Every acknowledged write is replayed | Integration | None found | none | Gap |
| TC-248 | The final snapshot is written | Writes; `SIGTERM`; read the `--dump` file after exit | The snapshot holds the keyspace after the last command; no temporary file left | Integration | None found (A-6) | none | Gap |
| TC-249 | The final save fails | `--dump` in a directory that cannot be written; `SIGTERM` | `final snapshot failed: <reason>` at critical level; exit non-zero; the previous snapshot is untouched | Integration | None found (A-6) | none | Gap |
| TC-250 | A server thread fails | An I/O thread, the accept loop or the housekeeping task fails | The failure is logged; shutdown runs from step 3; exit non-zero | Unit, Integration | None found (the listener tests check only that a clean stop reports no failure) | none | Gap |
| TC-251 | Killed, not stopped | `SIGKILL` with `--appendonly`, then with a snapshot only; restart | No final save; recovery from the log (at most the unsynced writes lost) or from the last snapshot | Integration | `aof` torn-tail and `save_file` atomic-replace unit tests cover the pieces | Unit | Partial |
| TC-252 | Log lines survive the stop | Messages queued in the async logger; shutdown | Everything queued is written out before exit | Unit | `lifecycle`: "shutting down the async logger writes out everything queued" | Unit | Covered |
| TC-262 | A FailFast end still saves | Housekeeping fails after 20 ticks (seam `REDIS_TEST_FAIL_HOUSEKEEPING_AFTER`) with `--dump` writable; a write before it; read the snapshot after exit | The snapshot holds the write; exit non-zero. Not yet covered: `--dump` unwritable (exit non-zero, previous file untouched) | Integration | `failfast_save`: "TC-262" (POSIX only, runs in `scripts/test_server.sh`) | none | Partial |
| TC-296 | Tearing down the keyspace executor waits for its work | A threaded `Executor` with a slow batch running and more queued; it is destroyed (or `stop()` is called) while they run | Every queued batch has finished before destruction returns; nothing runs on the keyspace thread afterwards. With `Services` in place of the `Executor`: the thread is joined before the keyspace it touches goes away, with no sanitizer report | Unit | None found (`executor`: "Running work on the keyspace thread" runs and finishes work, but never destroys the executor with work pending) | none | Gap |

## Entry and exit criteria

* **Entry**: the server builds; the unit suite runs in the chosen preset; for the Integration scenarios, a Linux or
  macOS runner (signals) with a scratch directory per run for the dump, log and append-only files.
* **Exit**: every scenario is Unit or higher, or has a Gap recorded in the master plan's anomaly list (section 16;
  A-2 and A-6 apply here).
