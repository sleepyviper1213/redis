# Use Case: Configure and Start the Server

## Use Case ID

UC-016

## Description

Operator starts the server with its settings, given as command-line options, in a config file, or as `REDIS_*`
environment variables. The server restores its data from disk, then starts accepting clients.

## Primary Actor

Operator (a person at a shell, a service manager such as systemd, a container runtime, the CI benchmark job)

## Preconditions

* The server binary is built for the platform
* The port is free, and the files named by `--dump`, `--appenddir` and `--log-file` can be read and written

## Postconditions

* The server listens on `--host`:`--port` and logs `Ready to accept connections`
* The keyspace holds what the append-only file (with `--appendonly`) or else the snapshot held; empty if neither
  exists

## Flow of Events

| Step | Actor    | Description                                                                                          |
|------|----------|------------------------------------------------------------------------------------------------------|
| 1    | Operator | Runs the server, for example `redis_server --port 6380 --appendonly --config server.toml`           |
| 2    | Server   | Reads the options from the command line, the environment and the config file                       |
| 3    | Server   | Starts logging to stderr and `--log-file`, and installs the crash handler                           |
| 4    | Server   | Logs `Server started, pid <pid>, port <port>, <n> I/O thread(s), dump file <file>`                   |
| 5    | Server   | With `--appendonly`, replays the append-only files in `--appenddir`; otherwise loads `--dump`        |
| 6    | Server   | Binds `--host`:`--port` and starts the I/O threads, the keyspace thread and the housekeeping task   |
| 7    | Server   | Logs `Ready to accept connections`                                                                   |

## Alternate Flows

### A1: Invalid option

* An unknown option, or a value that fails its check (for example `--host` that is not an IPv4 address, a port that is
  not a positive number, an `--appendfsync` policy other than `always`, `everysec` or `no`), is reported on stderr and
  the server exits non-zero without starting
* `--help` lists the options by group; `--version` prints the version

### A2: Data cannot be read

* A snapshot or append-only file that cannot be read, is not in this project's format, has another format version or
  is corrupt makes the server log the reason at critical level and exit non-zero
* It refuses to start rather than serve an empty keyspace and later overwrite the files with it (FR-018)

### A3: Append-only file ends in the middle of a command

* A log cut short by a crash is truncated to its last whole command, with a warning, and the server starts

### A4: Append-only directory in use

* When another server holds the lock on `--appenddir`, startup fails with
  `<dir> is in use by another server; refusing to share it`

### A5: Port in use

* The bind fails; the server logs the reason at critical level and exits non-zero

### A6: Log file cannot be opened

* The server logs to stderr only, and says so

## Related Functional Requirements

* FR-013: Configuration
* FR-018: Startup safety
* FR-011: Snapshots, FR-012: Append-only file (loading)
* FR-015: Logging

## Related Nonfunctional Requirements

* NFR-003: Reliability
* NFR-006: Maintainability
* NFR-007: Portability (Linux, macOS, Windows)

## Notes

* The options, their defaults and environment variables are listed in the README.
* With the append-only file on, it is the source of truth: it holds everything since the last rewrite, which the
  snapshot may not.
* Settings other than the log level cannot change while the server runs
  ([UC-015](uc_015_change_log_level.md)); restart it with new ones.
