# Software Design Description (SDD)

**Project:** Redis-compatible in-memory data store
**Document ID:** SDD-001
**Structure:** IEEE Std 1016-2009 [1] (design viewpoints)

## Document control

| Date        | Revision | Author             | Change        |
|-------------|----------|--------------------|---------------|
| 13 Nov 2026 | 1.0      | Khac Truong Nguyen | First version |

## Table of contents

1. [Introduction](#1-introduction)
2. [References](#2-references)
3. [Stakeholders and design concerns](#3-stakeholders-and-design-concerns)
4. [Context viewpoint](#4-context-viewpoint)
5. [Composition viewpoint](#5-composition-viewpoint)
6. [Logical viewpoint](#6-logical-viewpoint)
7. [Dependency viewpoint](#7-dependency-viewpoint)
8. [Information viewpoint](#8-information-viewpoint)
9. [Patterns viewpoint](#9-patterns-viewpoint)
10. [Interface viewpoint](#10-interface-viewpoint)
11. [Interaction viewpoint](#11-interaction-viewpoint)
12. [State dynamics viewpoint](#12-state-dynamics-viewpoint)
13. [Algorithm viewpoint](#13-algorithm-viewpoint)
14. [Resource viewpoint](#14-resource-viewpoint)
15. [Design rationale](#15-design-rationale)
16. [Requirements traceability](#16-requirements-traceability)
17. [Known limitations](#17-known-limitations)

---

## 1. Introduction

### 1.1 Purpose

This document describes the design of a Redis-compatible in-memory key-value server, so that a maintainer can
understand how the requirements in the Software Requirements Specification (SRS) [2] are met, and why the design is as
it is.

### 1.2 Scope

The design covers the server executable: networking, protocol, commands, keyspace, persistence, diagnostics and
configuration. Clients, `redis-benchmark` and the build system are outside it.

### 1.3 Intended audience

Maintainers and contributors; examiners reviewing the project.

### 1.4 Definitions

| Term             | Definition                                                                                  |
|------------------|---------------------------------------------------------------------------------------------|
| RESP             | Redis Serialization Protocol (version 2)                                                    |
| Batch            | The complete requests one read delivered, executed together and answered with one write     |
| Keyspace         | All keys, values and expiry deadlines                                                       |
| Keyspace thread  | The only thread that executes commands when `--io-threads` is above 1                       |
| Sender           | A stdexec (`std::execution`) description of asynchronous work that can be composed          |
| Strand           | An Asio executor that runs its handlers one at a time                                       |
| AOF              | Append-only file: a log of write commands replayed at startup                               |
| COW              | Copy-on-write: pages shared between a parent and a forked child until one writes            |

Module paths are relative to `src/`. Diagrams are in [docs/analysis/diagrams/](../analysis/diagrams/).

## 2. References

[1] *IEEE Standard for Information Technology—Systems Design—Software Design Descriptions*, IEEE Std 1016-2009, 2009.

[2] K. T. Nguyen, "Software Requirements Specification," `docs/analysis/software_requirement_specification.md`, 2026.

[3] K. T. Nguyen, "Use Case Descriptions," `docs/analysis/use_cases/README.md`, 2026.

[4] K. T. Nguyen, "Master Test Plan and Level Test Report," `docs/test/test_plan_and_report.md`, 2026.

[5] K. T. Nguyen, "Benchmark and Evaluation Report," `docs/evaluation/benchmark_report.md`, 2026.

[6] Redis Ltd., "Redis serialization protocol specification," https://redis.io/docs/latest/develop/reference/protocol-spec/.

[7] NVIDIA, "stdexec: `std::execution` reference implementation," https://github.com/NVIDIA/stdexec.

[8] K. T. Nguyen, "User and Installation Guide," `docs/user/user_guide.md`, 2026.

## 3. Stakeholders and design concerns

| Stakeholder          | Concerns                                                                                     |
|----------------------|----------------------------------------------------------------------------------------------|
| Client developer     | Works with existing Redis clients unchanged (NFR-004); correct, Redis-like replies           |
| Operator             | Starts, configures, monitors and stops the server safely; no data loss at shutdown (NFR-003) |
| Maintainer           | Readable, testable code; room to add commands and features (NFR-006, NFR-008)                |
| Examiner             | Requirements met; sound engineering choices, justified                                       |

| Design driver                | Consequence                                                                                       |
|------------------------------|---------------------------------------------------------------------------------------------------|
| Compatibility (NFR-004)      | RESP2; Redis' reply formats and error wording; `INFO` and `CONFIG GET` fields clients probe       |
| Throughput and latency       | One thread executes commands, with no locks; zero-copy arguments; one write per batch             |
| Reliability (NFR-003)        | Atomic file replacement, an append-only file, refusing to start on unreadable data                |
| Maintainability (NFR-006)    | One-way module dependencies, table-driven commands, errors as values                              |
| Portability (NFR-007)        | Portable C++ except `fork` and `fsync`, kept in `core/os`                                         |

## 4. Context viewpoint

**Services and actors.** The server offers the services in the use cases [3] to two actors: *Client* (any program that
speaks RESP or inline commands over TCP) and *Operator* (runs, configures and monitors the process). See
[use_case.puml](../analysis/diagrams/use_case.puml) and [dfd_context.puml](../analysis/diagrams/dfd_context.puml).

**Environment.** One process, one listening port, and three kinds of file on local disk
([deployment.puml](../analysis/diagrams/deployment.puml)):

| Resource               | Option           | Default             | Written by                         |
|------------------------|------------------|---------------------|------------------------------------|
| Snapshot               | `--dump`         | `dump.snap`         | `SAVE`, `BGSAVE`, shutdown         |
| Append-only directory  | `--appenddir`    | `appendonlydir`     | Every write, when `--appendonly`   |
| Log                    | `--log-file`     | `redis_server.log`  | Logger, crash handler              |

`BGSAVE` and `BGREWRITEAOF` fork a child process that exits when its file is written.

## 5. Composition viewpoint

The server is built from layers, one library per directory
([components.puml](../analysis/diagrams/components.puml)).

| Component              | Directory             | Responsibility                                                                  |
|------------------------|-----------------------|---------------------------------------------------------------------------------|
| Application            | `app/`                | Parse options, start logging and the crash handler, build and run the server    |
| Network                | `server/net/`         | Listener, sessions, request pipeline, executor, client limits                   |
| Commands               | `server/commands/`    | Dispatch by name, arity check, one file per command family                      |
| Persistence            | `server/persistence/` | Snapshot manager, append-only file, persistence errors                          |
| Diagnostics            | `server/diagnostics/` | Command watchdog, running-command record, crash reports                         |
| Keyspace               | `common/store/`       | Typed values, the database, expiry indexes, counting allocator                  |
| Protocol               | `common/resp/`        | RESP parser and encoder; request framing for RESP arrays and inline commands    |
| Foundation             | `core/`               | Error helpers, logging, OS wrappers, time, stdexec extensions, utilities        |

### 5.1 Decomposition of the larger components

* **`common/resp`:** an incremental parser that returns the bytes a request used or "incomplete"; a malformed request
  is a typed error; a client-declared length is bounded before anything is allocated. `Value` is a variant of simple
  string, error, integer, bulk string, array and the nulls; replies are encoded with `{fmt}` format specs.
* **`common/store`:** `Object` (string, list, set or hash), `Database` (the map and expiry indexes), a counting
  allocator so `INFO memory` reports real bytes, and, in debug builds, an owner-thread check.
* **`server/commands`:** `CommandInfo` (name, function, arity, write flag), one table per family (`string`, `list`,
  `hash`, `set`, `keyspace`, `server`), and the handler that merges the tables into one hash table.
* **`server/net`:** `Listener` (accept loop), `Session` (read, parse, execute, write), `Executor` (the keyspace
  thread), `Guard` (limits), admission.
* **`server/persistence`:** `SnapshotManager` (`SAVE`, `BGSAVE`, load, final save) and `AppendOnlyFile` (log,
  fsync policy, replay, manifest, rewrite).
* **`core`:** `error` (`REDIS_TRY`, `REDIS_FAILED`, `Failure<Kind>`), `logging` (channels `server`, `net`,
  `commands`, `persistence`), `os` (`fork_and_run`, `reap`, `SaveFile`, `fsync` family), `stdexec_ext` (`retry`,
  `every`, `supervise`, `InlineTask`), `chrono` (`DeadlineTimer`), `utils` (glob, number parsing, ring buffer).

## 6. Logical viewpoint

### 6.1 Keyspace map

The map is chosen at configure time with `-DREDIS_KEYSPACE_MAP=absl|boost|std`; the default is `absl::flat_hash_map`,
an open-addressing table with good cache behaviour. Keys are owned strings; lookups take `string_view`, so a command
never builds a key string to look one up.

### 6.2 Expiry indexes

| Index          | Structure                       | Answers                                                           |
|----------------|---------------------------------|-------------------------------------------------------------------|
| `expires_`     | hash map: key to deadline       | `TTL`, `PTTL`, `PERSIST` and "has a deadline" in O(1)             |
| `by_deadline_` | ordered set: (deadline, key)    | The sweep removes the oldest due keys in O(log n) each            |

### 6.3 Typed access

`find_as<T>` returns the object, `nullptr` for a missing key, or a `WrongType` failure. `find_or_create<T>` creates the
value on first write. A command never sees a value of the wrong type. An emptied list, set or hash is removed from
the keyspace by the command that emptied it, as in Redis.

### 6.4 Arguments and replies

Arguments are a `span` of `string_view` pointing into the session's read buffer, valid until the batch has finished.
Replies are `resp::Value` encoded directly into the output buffer. No argument is copied on the request path unless a
command stores it.

## 7. Dependency viewpoint

```text
app ──► server/net ──► server/commands ──► common/store
              │              └──────────► server/persistence ──► common/store
              └──────────────────────────────────────────────► common/resp, core
```

| Rule                                                                                                           | Reason                                              |
|----------------------------------------------------------------------------------------------------------------|-----------------------------------------------------|
| `core` never depends on `server`                                                                               | The foundation is reusable (NFR-009)                |
| `common/store` depends on `core` only                                                                          | The keyspace is testable without sockets or files   |
| Only `core/os` (`posix.cpp`, `windows.cpp`) calls `fork`, `waitpid` and `fsync`                                | Portability is one file per platform                |

External libraries: Boost.Asio (networking), stdexec (asynchronous composition), abseil and Boost.Unordered (hash
maps), spdlog and fmt (logging, formatting), CLI11 (options), reflect-cpp with CBOR (snapshot records), fast-float
(number parsing); Catch2 and hiredis for tests. All come from vcpkg ([vcpkg.json](../../vcpkg.json)).

## 8. Information viewpoint

### 8.1 Snapshot file

```text
magic    8 bytes   "RCPPSNAP"
version  uint32    2 (little-endian)
count    uint64    number of entries
entries  count x { size: uint64, record: size bytes of CBOR }
```

Each record is a CBOR map: the key, the absolute deadline if any, and the value tagged by type. Keys and values are
byte strings, so binary data is safe. Records are framed one by one, so saving holds one entry's copy at a time, and a
corrupt size cannot make loading allocate past the end of the file. Loading rejects another magic or version, or a
truncated record. The format is this project's own, not Redis' RDB.

### 8.2 Append-only directory

```text
appendonlydir/
  appendonly.manifest          which files make up the log
  appendonly.<n>.base.snap     a snapshot, in the format above
  appendonly.<n>.incr.aof      RESP commands since the base, or since the previous incremental file
  appendonly.lock              held for the server's lifetime
```

The state is the base plus the replay of each incremental file, in manifest order. Commands that cannot be replayed as
received are logged as an equivalent: `SET ... EX` as `PXAT`, `EXPIRE` as `PEXPIREAT`, `SPOP` as `SREM` of the members
taken.

### 8.3 Configuration

Every option is settable on the command line, in a `--config` file or in a `REDIS_*` environment variable
(FR-013). The options, defaults and variable names are in the user guide [8].

## 9. Patterns viewpoint

| Pattern                        | Where                                  | Purpose                                                                    |
|--------------------------------|----------------------------------------|----------------------------------------------------------------------------|
| Layered architecture           | Whole server                           | One-way dependencies                                                       |
| Table-driven dispatch          | `server/commands`                      | A new command is one row and one function                                  |
| Sender pipeline                | `server/net` session                   | Read, parse, execute, write as composed senders; structured cancellation   |
| Coroutine for branching loops  | Accept loop, housekeeping              | Clear control flow where a loop branches per connection, not per request   |
| Supervision                    | `RedisServer::run`                     | One failure policy per task: `Isolate` or `FailFast`                       |
| Single owner                   | Keyspace                               | No locks: one thread touches the data                                      |
| Expected / result type         | Parsing, persistence, I/O              | Errors are values, with structured kinds                                   |
| RAII                           | Logging guard, files, child processes  | Release on every path                                                      |
| Copy-on-write via `fork`       | `BGSAVE`, rewrite                      | A consistent snapshot without locking                                      |

## 10. Interface viewpoint

### 10.1 Client protocol

RESP2 over TCP. A request is an array of bulk strings, or an inline line. Replies are simple strings, errors,
integers, bulk strings, arrays and nulls. Supported commands and their replies are specified by the use cases [3]
and listed in the user guide [8]. Errors use Redis' wording (`ERR`, `WRONGTYPE`); a protocol error is answered, then
the connection is closed.

### 10.2 Operator interface

* **Options:** the command line, a config file or environment variables.
* **Signals:** `SIGINT` and `SIGTERM` stop the server gracefully.
* **Files:** the snapshot, the append-only directory and the log (section 4).
* **Logs:** one line per message with time, channel and level; a crash report between `REDIS BUG REPORT START` and
  `END` markers.

### 10.3 Internal interfaces

| Interface                      | Contract                                                                                       |
|--------------------------------|------------------------------------------------------------------------------------------------|
| `CommandFn`                    | `Value (CommandContext &, Args)`; arity is checked before the call                             |
| `CommandContext`               | The database, persistence, the append-only file if enabled, statistics, the guard, settings    |
| Persistence functions          | Return `std::expected<void, PersistError>`                                                     |
| Session stages                 | `read`, `parse_pending`, `execute_batch`, `write`; the first two and last touch only the session's own buffer |

## 11. Interaction viewpoint

### 11.1 Request path

Diagrams: [sequence_client_server_interaction.puml](../analysis/diagrams/sequence_client_server_interaction.puml),
[activity_command_execution_pipeline.puml](../analysis/diagrams/activity_command_execution_pipeline.puml).

1. The listener accepts a connection, admits it against `--maxclients`, and starts a session on its own strand.
2. The session reads into its buffer; `parse_pending` cuts out every complete request. A frame split across reads
   waits for more bytes; several pipelined frames form one batch.
3. `execute_batch` runs the batch: inline with one I/O thread; otherwise handed to the keyspace thread, with the
   session resuming on its strand afterwards.
4. The handler looks up the command, checks shape and arity, times it, and calls it.
5. The command reads or writes the database through typed lookups.
6. With an append-only file, a write command's log entry is made before any reply of the batch is sent.
7. Replies are encoded into the output buffer and sent with one write.

### 11.2 Startup and shutdown

Startup: [activity_startup.puml](../analysis/diagrams/activity_startup.puml). Load the append-only files, or else the
snapshot; bind the port; start the I/O threads, the keyspace thread and housekeeping; log "Ready to accept
connections". Failure to read data or to bind stops the server (FR-018).

Shutdown: [sequence_threads_request_and_shutdown.puml](../analysis/diagrams/sequence_threads_request_and_shutdown.puml).
On a signal: stop accepting and housekeeping; drain sessions; wait for the executing batch; cancel `BGSAVE` and an
unfinished rewrite; close the append-only file; write the final snapshot; exit. A failed final save exits non-zero.

### 11.3 Guarantees to clients

A batch is everything one read delivered. Its commands run one at a time, each to completion. A stop, a limit or a
failure skips a batch still queued, lets a running one finish, and drops the unsent reply. A batch stops part-way
only at the output-buffer limit. A client cannot know whether a command ran when its connection drops before the
reply, so retrying is safe only for idempotent commands. Detail: [directory_layout.md](../directory_layout.md).

## 12. State dynamics viewpoint

A client connection moves through the states in
[state_connection_lifecycle.puml](../analysis/diagrams/state_connection_lifecycle.puml): accepted or rejected at
admission; waiting for a request (bounded by `--idle-timeout`); reading and parsing; executing; writing (bounded by
`--request-timeout`); and closed, by the client, a limit, a protocol error or shutdown. Background work has its own
states:

| Process             | States                                                                                     |
|---------------------|--------------------------------------------------------------------------------------------|
| Background save     | none, running (child forked), finished (collected by housekeeping, outcome logged)         |
| AOF rewrite         | none, running (new incremental file in use), switched (new manifest written), failed       |
| Server              | loading, serving, stopping, stopped                                                        |

## 13. Algorithm viewpoint

### 13.1 Key expiry

* **Lazy:** a key found past its deadline when accessed is removed and counted in `expired_keys`.
* **Active:** every 100 ms the housekeeping tick removes up to 1000 due keys, oldest deadline first, using the ordered
  index. The bound keeps one tick short so clients are not held up; a key may stay in memory a little past its
  deadline, but no command sees it.
* A deadline already past, set by a command, deletes the key at once.

### 13.2 Atomic file replacement

Write a temporary file in the same directory; `fsync` it (`F_FULLFSYNC` on macOS); rename it over the target;
`fsync` the directory. A reader sees the old file or the new one, never a partial one.

### 13.3 Background save

`fork`; the child serialises its copy-on-write view and exits with `_exit`; the parent keeps serving. Housekeeping
collects the child with a non-blocking wait and logs the outcome.

### 13.4 Append-only file rewrite without buffering

1. Flush and sync the log. 2. Create a new incremental file and list it in the manifest; later writes go there.
3. Fork a child that writes the new base. 4. When the child succeeds, write a manifest listing only the new base and the
newest incremental file; delete the old files. Until step 4 the manifest lists the old files too, so a crash at any
point leaves a replayable log, and nothing is held in memory while the child works.

### 13.5 Pattern matching

`KEYS` and `CONFIG GET` use a glob matcher (`*`, `?`, `[abc]`, `[^a]`, ranges, `\`). `KEYS` visits every key, so it
blocks other clients for the time that takes.

### 13.6 Fsync policy

`always`: before the reply. `everysec`: a thread forces the log to disk once a second (default). `no`: left to the
operating system.

## 14. Resource viewpoint

### 14.1 Threads

Diagrams: [concurrency_threads.puml](../analysis/diagrams/concurrency_threads.puml).

| Thread          | Runs                                                              | Exists                              |
|-----------------|-------------------------------------------------------------------|-------------------------------------|
| Main            | startup load, I/O thread 0, final save                            | Always                              |
| I/O workers     | accept loop, signals, timers, sessions                            | `--io-threads` minus 1              |
| Keyspace        | every command batch, and the housekeeping tick                    | `--io-threads` above 1              |
| Watchdog        | reads the running-command record every 100 ms                     | `--watchdog-ms` above 0             |
| AOF fsync       | `fsync` of the log once a second                                  | `--appendfsync everysec`            |
| Logger          | writes queued log messages                                        | Always                              |

With `--io-threads 1` (default) the main thread does I/O and executes commands, as Redis does.

### 14.2 Synchronisation

The keyspace has no locks. Exactly one thread executes commands at any time, and housekeeping runs on it, so expiry and
reaping never race a command and every command is atomic (FR-009). Debug builds assert the owner thread.

### 14.3 Task supervision

`RedisServer::run()` races the accept loop, housekeeping and the wait for `SIGINT`/`SIGTERM`; the first to finish
cancels the others through a stop token.

| Task              | Policy        | A failure                              |
|-------------------|---------------|----------------------------------------|
| A session         | `Isolate`     | Logged; ends only that client          |
| The accept loop   | `FailFast`    | Ends the server with that error        |
| Housekeeping      | `FailFast`    | Ends the server with that error        |
| An I/O thread     | Outside tasks | Requests stop, then ends the server    |

Sessions are not in the race; they are cancelled through a counting scope and drained, then the event loop stops.

### 14.4 Limits

| Control                          | Effect                                                                                   |
|----------------------------------|------------------------------------------------------------------------------------------|
| `--maxclients`                   | A client over the limit gets `ERR max number of clients reached` and is closed           |
| `--client-query-buffer-limit`    | A client with too much unparsed input is dropped                                         |
| `--client-output-buffer-limit`   | A client with too many unsent reply bytes is dropped; later commands in the batch do not run |
| `--idle-timeout`                 | Drops a client that sends nothing for that long                                          |
| `--request-timeout`              | Drops a client whose request is not executed and answered in time                        |
| `--watchdog-ms`                  | Logs a warning while one command runs past the threshold                                 |

Violations are counted in `INFO stats` and `INFO clients`. Memory in use is counted by the allocator (`INFO memory`).

### 14.5 Error handling

* **No exceptions on the request path.** Parsing, persistence and I/O return `std::expected`. Exceptions are for what
  a library throws (for example Asio failing to bind) and for a crash.
* **Structured errors.** Each module has a kind enum (`ConnectionKind`, `PersistKind`, `ServerKind`) in a
  `Failure<Kind>`, with the system error, path and position where relevant; one `display` function produces the
  sentence, so a log line and an `ERR` reply agree.
* **Failures become replies.** A command failure is an error reply; a failed `SAVE` is `ERR` plus the reason and the
  previous file stays.
* **Failing to start beats starting wrong.** Unreadable data stops startup.

## 15. Design rationale

| #  | Decision                                            | Alternatives                              | Reason                                                                                              |
|----|-----------------------------------------------------|-------------------------------------------|-----------------------------------------------------------------------------------------------------|
| D1 | One thread executes commands, no locks              | Shards with a lock each                   | Redis' model; each command atomic; no lock cost; I/O still scales with `--io-threads`                |
| D2 | stdexec senders over Boost.Asio                     | Callbacks; coroutines everywhere          | Structured cancellation; no frame per request; the shutdown race is a few lines                     |
| D3 | Coroutines only for loops that branch per connection | Coroutines for the whole session         | A coroutine allocates a frame per call and turns errors into exceptions                             |
| D4 | Zero-copy arguments                                 | Copy each argument                        | Fewer allocations; the main reason large-value `GET` beats Redis in the benchmarks [5]              |
| D5 | Two expiry indexes                                  | One map with random sampling              | O(1) `TTL` and an ordered, bounded sweep                                                            |
| D6 | Own snapshot format (CBOR records)                  | Redis RDB                                 | RDB is large and version-bound; CBOR via reflect-cpp is small to maintain and binary-safe           |
| D7 | New incremental file before the rewrite fork        | Buffer writes during the rewrite          | No memory growth; a crash at any step leaves a replayable log                                       |
| D8 | Refuse to start on unreadable data                  | Start empty and log                       | Serving empty and later saving would destroy the only copy                                          |
| D9 | Errors as values                                    | Exceptions                                | Visible control flow, no hot-path cost, one source of wording                                       |
| D10| `fork` for background save and rewrite              | A thread that copies the keyspace         | Copy-on-write snapshot, no locking, no up-front copy; costs portability (POSIX only)               |

## 16. Requirements traceability

| Requirement                          | Design element                                                      | Section        |
|--------------------------------------|---------------------------------------------------------------------|----------------|
| FR-001 to FR-005 Data commands       | Command families over typed lookups                                 | 5, 6.3         |
| FR-006 Key expiry                    | Two indexes; lazy removal and bounded sweep                         | 6.2, 13.1      |
| FR-007 Server commands               | `server` command table; `INFO` sections                             | 5.1            |
| FR-008 TCP and protocol              | RESP parser, request framing, sessions                              | 5.1, 11.1      |
| FR-009 Concurrent clients            | I/O threads and a single keyspace owner                             | 14.1, 14.2     |
| FR-010 Error replies                 | `reply::*` helpers; handler checks                                  | 10.1, 14.5     |
| FR-011 Snapshots                     | Snapshot format; atomic replacement; background save                | 8.1, 13.2, 13.3 |
| FR-012 Append-only file              | Directory layout; fsync policy; rewrite                             | 8.2, 13.4, 13.6 |
| FR-013 Configuration                 | CLI11 options, config file, environment                             | 8.3            |
| FR-014 Client limits                 | Guard and admission                                                 | 14.4           |
| FR-015 Logging                       | Channels; asynchronous logger                                       | 5.1            |
| FR-016 Diagnostics                   | Watchdog; crash handler                                             | 14.1, 10.2     |
| FR-017 Graceful shutdown             | Supervision; shutdown order                                         | 11.2, 14.3     |
| FR-018 Startup safety                | A load failure is fatal                                             | 11.2, 14.5     |
| FR-019 Benchmarking                  | Scripts and the benchmark workflow                                  | [5]            |
| NFR-001 Performance                  | Zero-copy parsing; one write per batch; no locks                    | 6.4, 11.1      |
| NFR-003 Reliability                  | Atomic files; replayable log; startup safety                        | 13.2, 13.4     |
| NFR-005 Availability                 | Fork-based background work                                          | 13.3, 13.4     |
| NFR-006 to NFR-009 Quality           | Layers; tables; one-way dependencies                                | 7, 9           |

## 17. Known limitations

* **Single-thread execution:** a slow command (such as `KEYS` on a large keyspace) delays every client. The watchdog
  reports it; it does not prevent it.
* **Background work needs `fork`:** on Windows `BGSAVE` and `BGREWRITEAOF` reply with an error, and the crash handler
  is not installed.
* **No authentication** (FR-020): bind to a private address; leave `--enable-config-set` off unless the port is
  private.
* **Not implemented** (FR-021 to FR-025): Pub/Sub, transactions, Lua, replication and clustering, memory limit and
  eviction. Design notes for transactions and retries are in [TODO.md](../../TODO.md).
* **Snapshots are not RDB files:** tools that read RDB cannot read them.
