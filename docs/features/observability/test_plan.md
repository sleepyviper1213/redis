# Observability: Test Scenarios

Test scenarios for inspecting the server (`INFO`, `CONFIG GET`), changing the log level and watching the logs, derived
from the flows of the use cases [UC-014](use_cases/uc_014_inspect_server.md),
[UC-015](use_cases/uc_015_change_log_level.md) and [UC-018](use_cases/uc_018_watch_logs.md) of the
[Observability feature](README.md) (FR-007, FR-015, FR-016). They extend the
[Master Test Plan](../../test/test_plan_and_report.md), whose section 12 lists FR-007 and FR-015 as covered and FR-016
as partly covered (anomaly A-3).

Scenario IDs are `TC-nnn`, one sequence for the whole project ([ID ranges](../README.md#id-ranges)). Replies are quoted as the server writes them (RESP2 notation). The use cases have no
acceptance criteria, so scenarios come from their flows and alternate flows (A1, A2, ...).

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

| Level       | Use for                                                                                         |
|-------------|-------------------------------------------------------------------------------------------------|
| Unit        | `INFO` and `CONFIG` through the command handler; the stall detector with times the test chooses |
| Integration | A running server: log file content, a crash of a child process, tools such as redis-benchmark   |

Timing rules: assert counters by relation (rises by, at least) rather than exact values where time or memory is
involved; the watchdog unit tests judge the detector at chosen instants, so they need no real waiting.

## 1. Inspect the server with INFO (UC-014 main flow, A1)

| ID | Behaviour | Given / When | Expected | Level | Existing test | Tested at | Status |
|-----------|-----------|--------------|----------|-------|---------------|----------|----------|
| TC-193 | `INFO` returns the default sections as one bulk string | A server that has run some commands; `INFO` | A `# Name` line per section (`Server`, `Clients`, `Stats`, `Persistence`, `Keyspace`, and `Memory`), then `field:value` lines | Unit | `info`: "INFO reports the server's state" (five titles; `# Memory` only in its own scenario, not in the default reply) | Unit | Covered |
| TC-194 | Chosen sections only | `INFO keyspace`; `INFO clients stats` | Only those sections, in the order asked | Unit | `info`: "INFO reports the server's state" (one section) | Unit | Covered |
| TC-195 | Section selectors | `INFO CLIENTS`; `INFO all`; `INFO everything`; `INFO default` | Names are case-insensitive; `all` and `everything` add `commandstats`; `default` gives the default sections | Unit, Differential | None found | none | Gap |
| TC-196 | Unknown section | `INFO nope` | `$0\r\n\r\n` (empty bulk string), as in Redis | Unit | `info`: "INFO reports the server's state" | Unit | Covered |
| TC-197 | `server` and `clients` fields | `INFO server clients` | `uptime_in_seconds`; `connected_clients`, `maxclients` | Unit | `info`: "INFO reports connections when there is a network" (clients; no check of `uptime_in_seconds`) | Unit | Covered |
| TC-198 | Commands are counted, unknown ones are not | `SET`, `SET`, `EXPIRE`, an unknown command, then `INFO` | `total_commands_processed:3` (`INFO` itself counts after it replies) | Unit | `info`: "INFO reports the server's state" | Unit | Covered |
| TC-199 | Lookups are counted | One existing and one missing key read | `keyspace_hits` and `keyspace_misses` each rise by 1 | Unit | `info`: "INFO counts lookups and times each command" | Unit | Covered |
| TC-200 | `commandstats` times each command | `GET` x2, `SET` x1; `INFO commandstats` | `cmdstat_get:calls=2,usec=...`; `cmdstat_set:calls=1,usec=...`; `usec_per_call` present | Unit | `info`: "INFO counts lookups and times each command" (`calls` and `usec`; `usec_per_call` not checked) | Unit | Covered |
| TC-201 | Memory follows the keyspace | Store a large value, then delete it | `used_memory` rises by at least the value and falls back; `used_memory_human` present | Unit | `info`: "INFO reports the memory the keyspace holds" | Unit | Covered |
| TC-202 | Connection counters | Guard admits one client and refuses another | `connected_clients:1`, `maxclients:1`, `rejected_connections:1`, `total_connections_received:2` | Unit | `info`: "INFO reports connections when there is a network" | Unit | Covered |
| TC-203 | `persistence` fields | Default server; server with `--appendonly` | `rdb_bgsave_in_progress`, `rdb_last_save_time`, `aof_enabled`; with the file, also `aof_rewrite_in_progress`, `aof_fsync_policy` | Unit | None found | none | Gap |
| TC-204 | `keyspace` line | Two keys, one with an expiry; and an empty keyspace | `db0:keys=2,expires=1`; no `db0` line when empty | Unit | `info`: "INFO reports the server's state" (non-empty case only) | Unit | Covered |
| TC-205 | Expiry and rate counters | Keys expire; ops run for a second | `expired_keys` counts them; `instantaneous_ops_per_sec` is reported | Unit, Integration | `info`: "INFO counts lookups and times each command" (`expired_keys:0` only); see also [key expiry](../key_expiry/test_plan.md) TC-121 | Unit | Partial |

## 2. Read settings with CONFIG GET (UC-014 A2, A3)

| ID | Behaviour | Given / When | Expected | Level | Existing test | Tested at | Status |
|-----------|-----------|--------------|----------|-------|---------------|----------|----------|
| TC-206 | `CONFIG GET` by name | `CONFIG GET loglevel`; `CONFIG GET dbfilename` | Array of name and value | Unit | `config`: "Changing the log level at runtime"; `snapshot` ("CONFIG GET asks for the snapshot file name") | Unit | Covered |
| TC-207 | Glob patterns, the six settings, no match | `CONFIG GET *`; `append*`; `nomatch`; `save`; `appendonly`; `dir` | Pairs for `appendonly`, `appendfsync`, `dbfilename`, `dir`, `loglevel`, `save` (always empty); no match gives an empty array; redis-benchmark's start-up probes (`save`, `appendonly`) succeed | Unit, Integration | None found for patterns, `save`, `appendonly`, `appendfsync`, `dir` | none | Gap |
| TC-208 | `CONFIG GET` works while `CONFIG SET` is disabled | Server without `--enable-config-set`; `CONFIG GET loglevel` | The setting is returned | Unit | `config`: "CONFIG SET is off unless the operator enables it" | Unit | Covered |
| TC-209 | Unsupported `CONFIG` form | `CONFIG`; `CONFIG RESETSTAT`; `CONFIG GET` (no pattern) | `-ERR CONFIG supports only 'CONFIG GET <pattern>' and 'CONFIG SET loglevel <level>'` | Unit | None found | none | Gap |

## 3. Change the log level (UC-015)

| ID | Behaviour | Given / When | Expected | Level | Existing test | Tested at | Status |
|-----------|-----------|--------------|----------|-------|---------------|----------|----------|
| TC-210 | A level applies everywhere | Server at `info`, started with the option on; `CONFIG SET loglevel debug` | `+OK`; the default logger and every channel are at `debug`; `CONFIG GET loglevel` reports it | Unit | `config`: "Changing the log level at runtime" | Unit | Covered |
| TC-211 | Every level is accepted | `trace`, `debug`, `info`, `warning`, `error`, `critical`, `off` | `+OK` each; `CONFIG GET loglevel` reports each | Unit | `config`: "Changing the log level at runtime" (`debug` only) | Unit | Covered |
| TC-212 | The change is logged at warning | `CONFIG SET loglevel error`, then read the log | `Log level changed to error by CONFIG SET` is in the log even at a high level | Integration | None found | none | Gap |
| TC-213 | Refused when not enabled (A1) | `config_set_enabled` false; `CONFIG SET loglevel trace` | `-ERR CONFIG SET is disabled; ...`; level unchanged | Unit | `config`: "CONFIG SET is off unless the operator enables it" | Unit | Covered |
| TC-214 | Off by default at start-up | Server started without `--enable-config-set` | `CONFIG SET loglevel` is refused | Integration | None found (the unit test sets the flag by hand; option parsing is anomaly A-2) | none | Gap |
| TC-215 | Another parameter (A2) | `CONFIG SET port 1` | `-ERR Unsupported CONFIG parameter: port` | Unit | `config`: "Changing the log level at runtime" | Unit | Covered |
| TC-216 | Unknown level (A3) | `CONFIG SET loglevel loud` | `-ERR Invalid argument 'loud' for CONFIG SET 'loglevel'`; level unchanged | Unit | `config`: "Changing the log level at runtime" | Unit | Covered |

## 4. Watch the logs (UC-018)

| ID | Behaviour | Given / When | Expected | Level | Existing test | Tested at | Status |
|-----------|-----------|--------------|----------|-------|---------------|----------|----------|
| TC-217 | A line carries time, channel and level; goes to stderr and the file | `--log-file` set; messages on the `server` and `net` channels | Each line is tagged with its channel and level, and is on stderr and in the file | Integration | `lifecycle` writes to a file but checks only that lines arrive | Unit | Partial |
| TC-218 | Asynchronous logging loses nothing at shutdown | 200 warning lines queued; the logger shut down | Every line is in the file | Unit | `lifecycle`: "shutting down the async logger writes out everything queued" | Unit | Covered |
| TC-219 | Warnings are flushed at once | A warning, then a kill of the process | The warning is in the file | Integration | None found | none | Gap |
| TC-220 | A slow command is reported once | Threshold 50 ms; a command that started at t0 | Not reported at t0+10 ms; reported at t0+60 ms with name, argument count and run time; not again while it runs; a later long command is reported too; nothing if it ended first | Unit | `command_watchdog`: "A command running past the threshold is reported once"; `watchdog` (`stdexec_ext`): "A stall detector reports work past its threshold" | Unit | Covered |
| TC-221 | A stalled command is reported while it still runs (A1) | A watchdog task and a command started long ago | One report naming the command, before the command ends | Unit | `command_watchdog`: "The watchdog reports a stalled command while it runs"; `watchdog`: "monitor() reports work stuck past its threshold", "A heartbeat slot says what work is running, consistently" | Unit | Covered |
| TC-222 | Warning wording | A report is logged | `Command '<name>' (<n> args) has been running for <ms> ms; no other command runs until it ends` | Unit | None found (tests check the stall's fields, not the logged text) | none | Gap |
| TC-223 | `--watchdog-ms 0` turns it off (A1) | Threshold 0 | The watchdog is disabled and its task completes at once | Unit | `command_watchdog`: "The watchdog reports a stalled command while it runs" (no threshold case) | Unit | Covered |
| TC-224 | Crash report (A2, POSIX) | A child process aborts or faults while a command runs | Between `=== REDIS BUG REPORT START` and `END`: signal, fault address, pid, running command, backtrace; core dump left | Integration | None found (anomaly A-3) | none | Gap |
| TC-225 | Uncaught exception and missing backtrace support (A2) | An uncaught exception; a C library with no backtrace | The message is reported before the report; the report points to the core dump | Integration | None found | none | Gap |
| TC-226 | Errors worth acting on are logged (A3) | A dropped connection, a protocol error; a failed save, rewrite or append | Client errors on the `net` channel at error level; persistence failures at error or critical | Integration | None found (the error texts are tested in `net_error` and `persist_error`, not the logging) | none | Gap |

Coverage per use case is in [traceability.md](traceability.md).

## Entry and exit criteria

* **Entry**: the server builds; the unit suite runs in the chosen preset; for POSIX crash scenarios, a platform with
  `fork` and signals (Linux or macOS through CI).
* **Exit**: every scenario is Unit or higher, or has a Gap recorded in the test plan's anomaly list (section 16);
  the timer-based watchdog tests pass under load (anomaly A-1).
