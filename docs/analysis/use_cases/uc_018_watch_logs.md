# Use Case: Watch the Logs

## Use Case ID

UC-018

## Description

Operator follows what the server does, and finds out why it misbehaved, from its log: lifecycle messages, client and
persistence errors, warnings from the command watchdog about commands that hold up every client, and a crash report if
the process dies.

## Primary Actor

Operator (a person tailing the log, a log collector, a service manager's journal)

## Preconditions

* Server is running, or has just crashed
* `--log-level` (or `CONFIG SET loglevel`, [UC-015](uc_015_change_log_level.md)) is low enough for the messages wanted

## Postconditions

* The operator knows which command was slow or which command was running at a crash, and has a backtrace to report

## Flow of Events

| Step | Actor    | Description                                                                                           |
|------|----------|-------------------------------------------------------------------------------------------------------|
| 1    | Operator | Reads stderr, or the file named by `--log-file` (default `redis_server.log`)                         |
| 2    | Server   | Writes each message asynchronously, tagged with its channel (such as `server` or `net`) and level     |
| 3    | Server   | While one command has run past `--watchdog-ms` (default 1000 ms), logs a warning naming it            |
| 4    | Operator | Finds the slow command, for example a `KEYS *` on a large keyspace, and changes the client's behaviour |

## Alternate Flows

### A1: Slow command

* The warning reads `Command '<name>' (<n> args) has been running for <ms> ms; no other command runs until it ends`,
  and is written while the command still runs, so a command that never ends is reported too
* `--watchdog-ms 0` turns the watchdog off

### A2: Crash (POSIX)

* On a fatal signal (such as `SIGSEGV` or `SIGABRT`), the server writes a report to stderr and the log file, between
  `=== REDIS BUG REPORT START` and `=== REDIS BUG REPORT END`: the signal, the fault address, the process id, the
  command that was running, and a backtrace
* An uncaught exception is reported with its message before the report
* The process then ends with the signal's default action, leaving a core dump where enabled
* Where the C library has no backtrace support, the report says so and points to the core dump
* On Windows there is no crash report

### A3: Errors worth acting on

* Client errors (a dropped connection, a protocol error) are logged on the `net` channel at error level
* Persistence failures (a failed background save, rewrite or append-only file write) are logged at error or critical
  level; see [UC-001](uc_001_save_snapshot.md) and [UC-013](uc_013_rewrite_aof.md)

## Related Functional Requirements

* FR-015: Logging
* FR-016: Diagnostics

## Related Nonfunctional Requirements

* NFR-003: Reliability
* NFR-006: Maintainability

## Notes

* Messages at warning level and above are flushed at once, so they survive a crash.
* `INFO commandstats` gives the time spent per command, for slow commands that stay under the watchdog threshold
  ([UC-014](uc_014_inspect_server.md)).
