# Software Requirements Specification (SRS)

# Table of Contents

<!-- TOC -->
* [Software Requirements Specification (SRS)](#software-requirements-specification-srs)
* [Table of Contents](#table-of-contents)
* [Document information](#document-information)
* [1. Introduction](#1-introduction)
  * [1.1 Purpose](#11-purpose)
  * [1.2 Scope](#12-scope)
    * [1.2.1 In Scope](#121-in-scope)
    * [1.2.2 Out of Scope](#122-out-of-scope)
  * [1.3 Definitions, Acronyms, and Abbreviations](#13-definitions-acronyms-and-abbreviations)
  * [1.4 References](#14-references)
  * [1.5 Overview](#15-overview)
* [2. Overall Description](#2-overall-description)
  * [2.1 Product Perspective](#21-product-perspective)
    * [2.1.1 Business Case and Operational Concept](#211-business-case-and-operational-concept)
    * [2.1.2 System Context and Fit](#212-system-context-and-fit)
  * [2.2 Product Functions](#22-product-functions)
  * [2.3 User Characteristics](#23-user-characteristics)
  * [2.4 Constraints](#24-constraints)
  * [2.5 Assumptions and Dependencies](#25-assumptions-and-dependencies)
  * [2.6 Risk Analysis](#26-risk-analysis)
  * [2.7 Acceptance Criteria](#27-acceptance-criteria)
* [3. Specific Requirements](#3-specific-requirements)
  * [3.1 External interface Requirements](#31-external-interface-requirements)
    * [3.1.1 User Interfaces](#311-user-interfaces)
    * [3.1.2 Hardware Interfaces](#312-hardware-interfaces)
    * [3.1.3 Software interfaces](#313-software-interfaces)
    * [3.1.4 Communication interfaces](#314-communication-interfaces)
  * [3.2 Design Constraints](#32-design-constraints)
    * [3.2.1 Standard Compliance](#321-standard-compliance)
    * [3.2.2 Accounting & Auditing Procedures](#322-accounting--auditing-procedures)
    * [3.2.3 Legal & Data Privacy Constraints](#323-legal--data-privacy-constraints)
    * [3.2.4 Resource Constraints](#324-resource-constraints)
  * [3.3 Functional Requirements](#33-functional-requirements)
  * [3.4 Nonfunctional Requirements](#34-nonfunctional-requirements)
    * [[NFR-001] Performance](#nfr-001-performance)
    * [[NFR-002] Security](#nfr-002-security)
    * [[NFR-003] Reliability](#nfr-003-reliability)
    * [[NFR-004] Application Affinity and Compatibility](#nfr-004-application-affinity-and-compatibility)
    * [[NFR-005] Availability](#nfr-005-availability)
    * [[NFR-006] Maintainability](#nfr-006-maintainability)
    * [[NFR-007] Portability](#nfr-007-portability)
    * [[NFR-008] Extensibility](#nfr-008-extensibility)
    * [[NFR-009] Reusability](#nfr-009-reusability)
  * [3.5 Object-Oriented Models](#35-object-oriented-models)
    * [3.5.1 Architecture Specification](#351-architecture-specification)
    * [3.5.2 Data Flow Diagrams](#352-data-flow-diagrams)
    * [3.5.3 State Diagram](#353-state-diagram)
    * [3.5.4 Deployment Diagram](#354-deployment-diagram)
    * [3.5.5 Activity Diagrams](#355-activity-diagrams)
    * [3.5.6 Use Case Diagram](#356-use-case-diagram)
    * [3.5.7 Sequence Diagrams](#357-sequence-diagrams)
    * [3.5.8 Concurrency Model](#358-concurrency-model)
* [4. Appendix](#4-appendix)
<!-- TOC -->

---

# Document information

| Date        | Revision | Author             | Change                                                        |
|-------------|----------|--------------------|---------------------------------------------------------------|
| 7 Aug 2025  | 3        | Khac Truong Nguyen |                                                               |
| 13 Nov 2026 | 4        | Khac Truong Nguyen | Brought in line with the implementation; requirement statuses |
| 13 Nov 2026 | 5        | Khac Truong Nguyen | References in IEEE style; links to the design, test, user and benchmark documents; use cases split into one file each |

---
# 1. Introduction

## 1.1 Purpose

This document specifies the software requirements for a Redis-compatible in-memory data store written from scratch in
C++23, with Boost.Asio for networking, stdexec senders for its asynchronous control flow and its own RESP parser. It
targets educational use, experimentation and performance studies.

Each functional requirement in section 3.3 carries a status: **Implemented** requirements describe what the server does
today; **Planned** ones are kept for the record and are not available.

## 1.2 Scope

### 1.2.1 In Scope

* 46 Redis commands across strings, lists, hashes, sets, the keyspace and the server (section 3.3).
* RESP2 over TCP, plus inline commands as sent by telnet and redis-benchmark; pipelined requests.
* Per-key expiry, both on access and by a periodic sweep.
* Persistence through snapshots (`SAVE`, `BGSAVE`, on shutdown) and an optional append-only file.
* Many concurrent clients on one or more network threads, with per-client limits.
* Structured logging, crash reports and a watchdog for long-running commands.

### 1.2.2 Out of Scope

* Clustering, replication, Pub/Sub, transactions (`MULTI`/`EXEC`) and Lua scripting.
* RESP3 and Redis' RDB snapshot format.
* Authentication and access control (`AUTH`, ACLs).
* A GUI or HTTP interface.

## 1.3 Definitions, Acronyms, and Abbreviations

| Term         | Definition                                                                                   |
|--------------|----------------------------------------------------------------------------------------------|
| RESP         | Redis Serialization Protocol; this server speaks version 2                                   |
| Inline command | A request sent as one line of space-separated words, without RESP framing (telnet)         |
| Pipelining   | Sending several requests without waiting for the replies; they are answered in order        |
| Batch        | The requests one read delivered, executed together and answered in one write                |
| AOF          | Append-only file: a log of every write command, replayed at startup                         |
| RDB          | Redis' own snapshot format; this server uses its own format instead                         |
| Snapshotting | Writing the whole keyspace to one file at a point in time                                   |
| Keyspace     | All keys and their values, with their expiry deadlines                                      |
| Keyspace thread | The one thread that executes commands when `--io-threads` is above 1                     |
| CRLF         | Carriage return followed by line feed (`\r\n`), RESP's line terminator                      |
| SRS          | Software Requirements Specification                                                          |
| CLI          | Command Line Interface                                                                       |
| FR / NFR     | Functional / Non-functional Requirement                                                      |
| GUI          | Graphical User Interface                                                                     |

## 1.4 References

[1] *IEEE Recommended Practice for Software Requirements Specifications*, IEEE Std 830-1998, 1998. (Superseded by
ISO/IEC/IEEE 29148:2018; the section structure of this document follows IEEE 830.)

[2] *Systems and software engineering—Life cycle processes—Requirements engineering*, ISO/IEC/IEEE Std 29148:2018,
2018.

[3] Redis Ltd., "Redis documentation," https://redis.io/docs/.

[4] Redis Ltd., "Redis serialization protocol specification,"
https://redis.io/docs/latest/develop/reference/protocol-spec/.

[5] C. M. Kohlhoff, "Boost.Asio," https://www.boost.org/doc/libs/release/doc/html/boost_asio.html.

[6] NVIDIA, "stdexec: `std::execution` reference implementation," https://github.com/NVIDIA/stdexec.

[7] "Build your own Redis with C/C++," https://build-your-own.org/redis/.

[8] K. T. Nguyen, "Use Case Descriptions," [use_cases/README.md](use_cases/README.md), 2026.

[9] K. T. Nguyen, "Software Design Description," [../design/software_design_document.md](../design/software_design_document.md), 2026.

[10] K. T. Nguyen, "Master Test Plan and Level Test Report," [../test/test_plan_and_report.md](../test/test_plan_and_report.md), 2026.

[11] K. T. Nguyen, "User and Installation Guide," [../user/user_guide.md](../user/user_guide.md), 2026.

[12] K. T. Nguyen, "Benchmark and Evaluation Report," [../evaluation/benchmark_report.md](../evaluation/benchmark_report.md), 2026.

[13] K. T. Nguyen, "Directory Layout," [../directory_layout.md](../directory_layout.md), 2026.

## 1.5 Overview

Section 2 gives a high-level overview; section 3 details the interfaces, design constraints, functional and
non-functional requirements and the design models; section 4 lists the supporting documents.

---

# 2. Overall Description

## 2.1 Product Perspective

### 2.1.1 Business Case and Operational Concept

The project is a lightweight, high-performance in-memory key-value store modelled on Redis. It implements a subset of
the Redis command set closely enough that standard clients (redis-cli, hiredis, redis-benchmark) work against it
unchanged, and its replies are checked against a real Redis server by a differential test. It serves education,
research and prototyping, where a small server that can be read end to end and changed easily is worth more than
Redis' full feature set.

A single server process listens on a TCP port. Clients send commands to store, read, expire and delete data; the
server keeps everything in memory and persists it to disk on demand (`SAVE`, `BGSAVE`), on shutdown, and, when the
append-only file is enabled, by logging every write.

The project is also a platform for studying system-level programming: asynchronous I/O with Boost.Asio, sender-based
concurrency with stdexec, memory layout of the keyspace, and the architecture of a server whose commands run on one
thread while its network I/O runs on several.

### 2.1.2 System Context and Fit

The system is not meant to replace Redis in production. It fits:

* Development and test environments that need a small, readable Redis-compatible server
* Teaching operating systems, networking and database courses
* Performance experiments, through its benchmark and profiling workflows

It operates on its own: it has no dependency on other services, and other programs reach it only through the Redis
protocol. In a larger system it may serve as a local cache or volatile store.

## 2.2 Product Functions
The system provides the following high-level functionalities:

| **ID** | **Requirement**                       | **Description**                                                                                  | **Priority** | **Status**  |
|--------|---------------------------------------|--------------------------------------------------------------------------------------------------|--------------|-------------|
| BR-001 | Fast Key-Value Data Storage           | Clients store and retrieve key-value data with minimal latency.                                  | High         | Implemented |
| BR-002 | In-Memory Performance                 | Data is kept in memory for real-time reads and writes.                                           | High         | Implemented |
| BR-003 | Command-Based Interface               | Clients use the Redis protocol (RESP2), or inline commands for manual use.                       | High         | Implemented |
| BR-004 | Persistence                           | Snapshots on demand and on shutdown; an optional append-only file logs every write.              | Medium       | Implemented |
| BR-005 | Basic Data Types Support              | Strings, lists, sets and hashes, each with optional expiry.                                      | Medium       | Implemented |
| BR-006 | Multi-Client Access                   | Many concurrent client connections over TCP, on one or more network threads.                     | High         | Implemented |
| BR-007 | Minimal Resource Footprint            | Lightweight enough for constrained environments; per-client limits bound memory use.             | Medium       | Implemented |
| BR-008 | Configurable Settings                 | Operators configure the server by command line, configuration file or environment.               | Low          | Implemented |
| BR-009 | Extensibility for Additional Features | The design admits new features such as Pub/Sub or replication later.                             | Medium       | Planned     |
| BR-010 | Cross-Platform Compatibility          | Builds and runs on Linux and macOS, both built and tested in CI. Windows is not supported.        | Low          | Implemented |

Diagram: Use Case Diagram ([use_case.puml](diagrams/use_case.puml))

## 2.3 User Characteristics

| User Type | Characteristics                                                                          |
|-----------|------------------------------------------------------------------------------------------|
| Developer | C++ programmers studying or extending a key-value server                                 |
| Tester    | Uses redis-cli, hiredis clients or redis-benchmark against the server                    |
| Operator  | Starts and stops the server, sets its options, reads its logs                            |
| Student   | Learns protocol parsing, asynchronous I/O and concurrency from the code                  |

## 2.4 Constraints

* **Memory Constraints**: all keys and values are held in memory, so the dataset is limited by available RAM. There is
  no eviction policy (`maxmemory`) and no paging.
* **Operational Constraints**:
  * Single-node operation: no clustering or replication.
  * Commands execute one at a time, on a single thread, as in Redis: one slow command (for example `KEYS` on a large
    keyspace) holds up every client until it ends. The watchdog reports such commands (FR-016).
  * `SAVE` blocks every client while it writes; `BGSAVE` forks a child and does not.
  * Without the append-only file, writes since the last snapshot are lost if the process is killed; with it, at most
    the writes of the last second are lost under the default `appendfsync everysec`.
  * The server follows a client/server model over TCP.
* **Site Adaptation Constraints**:
  * Supported on Linux and macOS, where continuous integration builds the server and runs every test suite.
  * Windows is not supported: no CI job builds or tests it, so nothing shows that the server works there. The code has
    Windows branches (no `fork`, so no background saves), but they are unverified.
  * Adapting to constrained environments may require recompiling with other keyspace map settings.
  * No built-in container orchestration support; the server is a single executable with file-based state.

## 2.5 Assumptions and Dependencies

* Commands and replies follow RESP2; lines end with CRLF. Values in RESP bulk strings are binary-safe; inline commands
  are not, and are meant for manual use.
* The server runs on a trusted network or on localhost: it has no authentication (it listens on `127.0.0.1` by default).
* The working directory holds enough disk space for the snapshot and the append-only file, which can each be about as
  large as the dataset.
* The filesystem honours `fsync` and atomic `rename`, on which snapshot and append-only file safety rely.

## 2.6 Risk Analysis

This section identifies the risks in developing and operating the server, their impact and likelihood, and how they
are mitigated.

| Risk ID | Risk Description                                           | Impact                                                    | Likelihood | Mitigation                                                                                                                                                                                                     |
|---------|------------------------------------------------------------|-----------------------------------------------------------|------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| R-001   | Snapshot file corrupted during save or load                | High: data loss, or no restore                            | Low        | Write to a temporary file, `fsync`, `rename`, then `fsync` the directory, so readers see the old file or the new one, never a partial one; a magic number and format version are checked on load; the server refuses to start rather than serve an empty keyspace (FR-011). A checksum is planned. |
| R-002   | Dependency issues (versions, platforms)                    | Medium: build failures                                    | Medium     | Dependencies pinned through a vcpkg baseline; CMake presets per toolchain; CI builds on Linux (GCC, Clang) and macOS (NFR-007).                                                                                 |
| R-003   | Performance bottlenecks under many connections             | High: latency targets missed (NFR-001)                    | Medium     | Asynchronous I/O; several network threads; replies of a batch sent in one write; benchmarked against Redis with redis-benchmark in CI.                                                                          |
| R-004   | Malformed requests crashing the server                     | High: instability or downtime                             | Low        | Bounded parser (declared lengths checked); protocol errors answered then the client closed; query and output buffer limits (FR-010, FR-014); sanitizer and stress runs in CI.                                   |
| R-005   | Too little logging to diagnose issues                      | Medium: slow debugging                                    | Low        | Logging channels per module; crash reports with the running command and a backtrace (FR-015, FR-016).                                                                                                          |
| R-006   | Limited expertise in C++23, Boost.Asio or stdexec          | Medium: delays, poor code quality                         | Medium     | Modular design, documented in the code and in [directory_layout.md](../directory_layout.md); static analysis in CI (NFR-006).                                                                                  |
| R-007   | Snapshot saving blocking clients                           | High: unavailability during `SAVE` (NFR-005)              | Medium     | `BGSAVE` saves in a forked child while the server keeps serving (POSIX); `SAVE` still blocks, as in Redis.                                                                                                     |
| R-008   | Unhandled client input                                     | Medium: crash or wrong replies                            | Low        | Typed lookups return `WRONGTYPE`; integers parsed as Redis does; differential test against Redis; property-style stress test.                                                                                   |
| R-009   | One long-running command stalling every client             | High: the whole server appears hung                       | Medium     | The command watchdog logs a command running past `--watchdog-ms` while it is still running (FR-016).                                                                                                            |

## 2.7 Acceptance Criteria

* Unit tests cover the core logic: parsing, command execution, persistence, sessions and the concurrency helpers.
* Integration tests drive a running server through hiredis, with one and with four network threads.
* A stress test checks every reply against a model and races clients on shared keys.
* A differential test sends the same random commands to this server and to Redis and compares the replies.
* All of the above pass under ThreadSanitizer in CI; AddressSanitizer and UndefinedBehaviorSanitizer presets are
  available for local runs.
* Architecture, use cases, concurrency and behaviour are documented with UML diagrams (section 3.5).
* The README gives build instructions, usage examples and every server option.

---

# 3. Specific Requirements

## 3.1 External interface Requirements

### 3.1.1 User Interfaces

* No GUI. Users interact through any Redis client (redis-cli, hiredis-based programs) or telnet with inline commands.
* The server itself is configured through its command-line interface (`redis_server --help`), a configuration file
  (`--config`) or `REDIS_*` environment variables.

### 3.1.2 Hardware Interfaces

* None required: a general-purpose 64-bit CPU and enough RAM for the dataset.

### 3.1.3 Software interfaces

* Operating systems: Linux, macOS (Windows is not supported; see section 2.4)
* Compilers: GCC or Clang with C++23 support (CI uses GCC 16 and LLVM 22; Homebrew LLVM on macOS)
* Language: ISO C++23
* Build system: CMake 3.28+ with presets; dependencies through vcpkg
* Libraries:
    * Boost.Asio for networking, Boost.Unordered and Abseil for the keyspace hash maps
    * stdexec (with its Asio integration) for senders and coroutines
    * spdlog and {fmt} for logging and formatting
    * CLI11 for options, magic_enum for enum names, fast_float for number parsing
    * cereal for the snapshot's portable binary encoding
    * Catch2 for unit tests, hiredis for integration tests

### 3.1.4 Communication interfaces

* TCP, by default on `127.0.0.1:6379` (`--host`, `--port`).
* RESP2 requests (arrays of bulk strings) or inline commands; RESP2 replies.
* Pipelined requests are answered in order; the replies to the requests one read delivered are sent in one write.

Diagram: Client Connection State Machine ([state_connection_lifecycle.puml](diagrams/state_connection_lifecycle.puml))

The state machine follows one client connection: admission (refused past `--maxclients`), waiting for a request,
parsing, executing, writing, and every way a connection closes: the client closing it, a protocol error, a buffer
limit, the idle or request timeout, or server shutdown.

Diagram: Client-Server Interaction ([sequence_client_server_interaction.puml](diagrams/sequence_client_server_interaction.puml))

The sequence diagram shows a client connecting, sending two pipelined requests in one write, the server executing
both and answering in one write, a `WRONGTYPE` error, and the connection ending.

## 3.2 Design Constraints

### 3.2.1 Standard Compliance

* **RESP Protocol Specification**
  The server complies with **RESP version 2** for requests and replies, so standard Redis clients work unchanged:
  simple strings, errors, integers, bulk strings and arrays, including null bulk strings and null arrays. Error texts
  follow Redis' wording, checked by the differential test.

* **ISO C++ Standards**
  The implementation follows **ISO/IEC 14882:2024 (C++23)**. Code avoids undefined behaviour and data races; CI runs the
  sanitizers and static analysis (clang-tidy, cppcheck).

* **POSIX**
  Background saves, crash reports and file durability use POSIX (`fork`, `waitpid`, signals, `fsync`), which the
  supported platforms provide.

* **Filesystem Access**
  File paths go through `std::filesystem`; snapshots and the append-only file are written so a crash never leaves a
  partial file in place.

* No external database or storage engine other than the snapshot and the append-only file.
* No Redis source code: every component is implemented from scratch.

### 3.2.2 Accounting & Auditing Procedures

The server logs enough to support debugging and post-mortem analysis, without recording client data.

* **Logging channels**: `server`, `net`, `commands` and `persistence`, at levels `trace` to `critical`
  (`--log-level`), to stderr and to `--log-file`. Logging is asynchronous, so writing a log line never waits for disk.
* **Commands**: at `trace` level, each command's name and argument count, never its arguments or reply, since those
  are the clients' data.
* **Connections**: every limit a client breaks (too many clients, a buffer limit, an idle or request timeout) is
  logged with the client's address and counted for `INFO`.
* **Persistence**: every save, background save, rewrite and append-only file problem is logged with its outcome and
  file.
* **Crashes**: a fatal signal or uncaught exception writes a report with the command that was running and a backtrace
  (POSIX).
* **Long-running commands**: the watchdog logs the command name and how long it has run (FR-016).
* **Audit readability**: logs are plain text, one line per event. Retention and rotation are left to the operator.

---

### 3.2.3 Legal & Data Privacy Constraints

* **User Data Protection**
  Keys, values and command arguments are never written to the log. There are no credentials to protect: the server has
  no authentication (see NFR-002). The append-only file and its directory are created readable by their owner only.

* **Third-party Library Licensing**
  All third-party libraries are used under permissive licences (Boost Software License, MIT, BSD, Apache 2.0). No GPL or
  other copyleft dependency is used.

### 3.2.4 Resource Constraints

* The server must stay lightweight:
  * CPU: one core suffices; `--io-threads` uses more for network I/O
  * RAM: the dataset plus per-client buffers, bounded by `--client-query-buffer-limit` and `--client-output-buffer-limit`
  * Disk: snapshots are written only on request (`SAVE`, `BGSAVE`) and at shutdown; there is no automatic interval

---

## 3.3 Functional Requirements

| ID     | Requirement                    | Description                                                                                                                                                                                 | Priority | Status      |
|--------|--------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|----------|-------------|
| FR-001 | String commands                | `SET` (with `EX`, `PX`, `EXAT`, `PXAT`, `KEEPTTL`, `NX`, `XX`, `GET`), `GET`, `MSET`, `MGET`, `APPEND`, `STRLEN`, `INCR`, `DECR`, `INCRBY`, `DECRBY`.                                                    | High     | Implemented |
| FR-002 | List commands                  | `LPUSH`, `RPUSH`, `LPOP`, `RPOP` (with a count), `LLEN`, `LRANGE`, `LINDEX`.                                                                                                                 | High     | Implemented |
| FR-003 | Hash commands                  | `HSET`, `HGET`, `HMGET`, `HDEL`, `HGETALL`, `HLEN`, `HEXISTS`.                                                                                                                               | Medium   | Implemented |
| FR-004 | Set commands                   | `SADD`, `SREM`, `SCARD`, `SMEMBERS`, `SISMEMBER`, `SINTER`, `SPOP`.                                                                                                                          | Medium   | Implemented |
| FR-005 | Keyspace commands              | `DEL`, `EXISTS`, `KEYS` (glob patterns), `TYPE`, `TTL`, `PTTL`, `PERSIST`, `FLUSHDB`, `DBSIZE`.                                                                                              | High     | Implemented |
| FR-006 | Key expiry                     | A key with a deadline disappears once it passes: on access, and by a sweep every 100 ms that removes up to 1000 expired keys, oldest deadline first.                                       | High     | Implemented |
| FR-007 | Server commands                | `PING`, `ECHO`, `INFO` (sections `server`, `clients`, `memory`, `persistence`, `stats`, `commandstats`, `keyspace`), `CONFIG GET <pattern>`, `CONFIG SET loglevel`.                           | Medium   | Implemented |
| FR-008 | TCP server and protocol        | Listen on `--host`:`--port`; parse RESP2 arrays and inline commands; handle requests split across reads and pipelined requests in one read.                                                | High     | Implemented |
| FR-009 | Concurrent clients             | Serve many clients at once with asynchronous I/O on `--io-threads` threads; above one, commands execute on a single keyspace thread, so they never run concurrently.                        | High     | Implemented |
| FR-010 | Error replies                  | Redis' errors for an unknown command, wrong arity, `WRONGTYPE`, a value that is not an integer, overflow and syntax errors; a protocol error is answered, then the connection is closed.    | High     | Implemented |
| FR-011 | Snapshots                      | `SAVE`, `BGSAVE` (forked child, POSIX), `LASTSAVE`; written atomically to `--dump` (default `dump.snap`) in this project's format; loaded at startup; saved at shutdown.                    | Medium   | Implemented |
| FR-012 | Append-only file               | `--appendonly` logs every write to `--appenddir` (manifest, base snapshot, incremental log); `--appendfsync always/everysec/no`; `BGREWRITEAOF` compacts it; it is replayed at startup.    | Medium   | Implemented |
| FR-013 | Configuration                  | Every option settable on the command line, in a `--config` file or through `REDIS_*` environment variables; see the README for the list.                                                    | Low      | Implemented |
| FR-014 | Client limits                  | `--maxclients`, `--client-query-buffer-limit`, `--client-output-buffer-limit`, `--idle-timeout`, `--request-timeout`; a client past a limit is disconnected and the event counted in `INFO`. | Medium   | Implemented |
| FR-015 | Logging                        | Asynchronous logging by channel to stderr and `--log-file`; level set by `--log-level`, or at run time by `CONFIG SET loglevel` when `--enable-config-set` is given.                      | High     | Implemented |
| FR-016 | Diagnostics                    | Crash reports with the running command and a backtrace (POSIX); a watchdog that logs a command running past `--watchdog-ms` (default 1000 ms) while it still runs.                         | Medium   | Implemented |
| FR-017 | Graceful shutdown              | On `SIGINT` or `SIGTERM`: stop accepting, finish or drop pending work, save a final snapshot; exit non-zero if that save fails.                                                              | High     | Implemented |
| FR-018 | Startup safety                 | Refuse to start when the snapshot or append-only file cannot be read, rather than serve an empty keyspace and overwrite the files later.                                                   | High     | Implemented |
| FR-019 | Benchmarking                   | Throughput and latency measured with redis-benchmark against this server and Redis, in CI (`bench/` branches).                                                                              | Low      | Implemented |
| FR-020 | Authentication and permissions | `AUTH` and client permissions.                                                                                                                                                             | Low      | Planned     |
| FR-021 | Pub/Sub                        | Publish/subscribe messaging.                                                                                                                                                               | Low      | Planned     |
| FR-022 | Transactions                   | `MULTI`, `EXEC`, `WATCH`.                                                                                                                                                                  | Low      | Planned     |
| FR-023 | Lua scripting                  | `EVAL` and script execution.                                                                                                                                                               | Low      | Planned     |
| FR-024 | Clustering or replication      | Distributed setups.                                                                                                                                                                        | Low      | Planned     |
| FR-025 | Memory limit and eviction      | `maxmemory` with an eviction policy.                                                                                                                                                       | Low      | Planned     |

---

## 3.4 Nonfunctional Requirements

### [NFR-001] Performance

Targets, measured with redis-benchmark (FR-019):

* `GET`/`SET` latency: ≤ 1 ms at 1,000 concurrent connections
* Throughput: ≥ 10,000 operations per second on a 4-core CPU
* Cold start: ≤ 500 ms with a snapshot under 10 MB
* Parsing: well under 5 ms per request; pipelined requests answered in one write per batch
* `SAVE` blocking time: ≤ 100 ms for small datasets; `BGSAVE` does not block
* Per-command overhead of diagnostics: a few memory stores (the watchdog's heartbeat)

### [NFR-002] Security

* Malformed and unknown commands are rejected with an error; a protocol error closes the connection.
* Declared lengths are bounded, and per-client buffer limits stop a client from exhausting memory.
* Listens on `127.0.0.1` by default; there is no authentication, so it must not be exposed to untrusted networks.
* `CONFIG SET` is disabled unless `--enable-config-set` is given.
* Files are replaced atomically; the append-only file is private to its owner.

### [NFR-003] Reliability

* Recovery after a crash from the latest snapshot, or from the append-only file when enabled.
* Snapshots and append-only file rewrites never leave a partial file in place.
* The server refuses to start on unreadable persistence files (FR-018), and fails loudly when the final save fails.
* Correctness is checked by unit, integration, stress and differential tests in CI, including under ThreadSanitizer.

### [NFR-004] Application Affinity and Compatibility

* Works with standard Redis clients: redis-cli, redis-benchmark, hiredis and libraries built on it.
* Replies, including error texts, match Redis for the implemented commands, as the differential test checks.

### [NFR-005] Availability

* Keeps serving clients during `BGSAVE` and `BGREWRITEAOF`, which run in forked children (POSIX).
* A client that misbehaves is disconnected without affecting others (FR-014).
* A long-running command is reported while it runs (FR-016), though it still holds up every client, as in Redis.

### [NFR-006] Maintainability

* One library per directory: `core` (no networking, no server state), `common` (RESP, keyspace), `server` (`net`,
  `commands`, `persistence`, `diagnostics`), `app`; dependencies point one way only.
* Parsing, command logic, transport and persistence are separate layers.
* Static analysis (clang-tidy, cppcheck) and formatting rules (clang-format) are part of CI.

### [NFR-007] Portability

* Builds with CMake presets on Linux (GCC, Clang) and macOS (Homebrew LLVM); CI builds and tests both.
* Platform-specific code (`fork`, signals, crash reports) is isolated, so another platform could be added; Windows is
  not supported until CI builds and tests it.

### [NFR-008] Extensibility

* Commands are table-driven: a new command is one function and one entry in its family's `CommandInfo` list.
* Reusable stdexec algorithms (`retry`, `supervised`, `every`, `watchdog`) for new background work.

### [NFR-009] Reusability

* `core` and `common` (RESP parser and encoder, typed keyspace, logging, sender algorithms) are self-contained libraries
  usable outside the server.

---

## 3.5 Object-Oriented Models

This section presents the design models that describe the structure, behaviour, concurrency and deployment of the
server. Each diagram is kept in the [`diagrams/`](diagrams/) folder as PlantUML.

---

### 3.5.1 Architecture Specification

* [`components.puml`](diagrams/components.puml)

The component diagram shows the libraries and their main parts:

* `net`: the listener (accept loop, admission), sessions (read, parse, write), the executor (keyspace thread) and the
  client limits
* `commands`: the command handler (lookup, arity, dispatch) and the six command families
* `persistence`: the snapshot manager and the append-only file
* `diagnostics`: the command watchdog and the crash handler
* `common`: the RESP parser and encoder, and the typed keyspace
* `core`: sender algorithms, logging, time, OS and utility code

---

### 3.5.2 Data Flow Diagrams

The data flow is modelled at three levels:

* [`dfd_context.puml`](diagrams/dfd_context.puml)
  Level 0: the server with its external entities (client, operator) and its data stores (snapshot, append-only file,
  log file).

* [`dfd_subprocess.puml`](diagrams/dfd_subprocess.puml)
  Level 1: reading and parsing, dispatching, executing, persisting and housekeeping, with the keyspace, files and
  statistics they read and write.

* [`dfd_execute.puml`](diagrams/dfd_execute.puml)
  Level 2: process 3.0, executing a command, broken down by command family, with the typed lookups each one uses and
  what each writes to the append-only file.

---

### 3.5.3 State Diagram

* [`state_connection_lifecycle.puml`](diagrams/state_connection_lifecycle.puml)

The lifecycle of one client connection, from admission through waiting, parsing, executing and writing, to every way it
closes, including where each timeout applies.

---

### 3.5.4 Deployment Diagram

* [`deployment.puml`](diagrams/deployment.puml)

The server process on Linux or macOS; the clients that reach it over TCP; its files (`dump.snap`,
`appendonlydir/`, the log); and the child process that background saves fork.

---

### 3.5.5 Activity Diagrams

* [`activity_command_execution_pipeline.puml`](diagrams/activity_command_execution_pipeline.puml)
  One connection's rounds: reading, parsing a batch, executing each request with its checks, logging writes and
  answering in one write, and where each limit closes the connection.

* [`activity_startup.puml`](diagrams/activity_startup.puml)
  Startup and shutdown: options, logging, loading persistence (or refusing to start), listening, the concurrent tasks
  while serving, and the shutdown order down to the final save.

---

### 3.5.6 Use Case Diagram

* [`use_case.puml`](diagrams/use_case.puml)

The use cases of the client (storing and reading data, expiry, persistence, inspecting the server) and of the operator
(configuring, stopping the server, watching the logs), with the future extension point for features such as Pub/Sub.
Each has its own description, indexed in [use_cases/README.md](use_cases/README.md).

---

### 3.5.7 Sequence Diagrams

* [`sequence_client_server_interaction.puml`](diagrams/sequence_client_server_interaction.puml)
  A full interaction: admission, two pipelined requests answered in one write, a `WRONGTYPE` error, disconnection.

* [`sequence_connection_ping_pong.puml`](diagrams/sequence_connection_ping_pong.puml)
  `PING` and its replies, with the idle timeout and the client limit.

---

### 3.5.8 Concurrency Model

* [`concurrency_threads.puml`](diagrams/concurrency_threads.puml)
  Every thread and child process: what each runs, when it exists, what it hands to which, and the state they share
  (the lock-free keyspace with one owning thread, the watchdog's heartbeat).

* [`sequence_threads_request_and_shutdown.puml`](diagrams/sequence_threads_request_and_shutdown.puml)
  One request crossing from a network thread to the keyspace thread and back, and the order in which the threads stop
  at shutdown.

---

# 4. Appendix

* A. Use Case Descriptions
  One file per use case, indexed in [use_cases/README.md](use_cases/README.md)

* B. Test Plan and Report
  See [test_plan_and_report.md](../test/test_plan_and_report.md) (this replaces the unchecked
  [TEST_CHECKLIST.md](../test/TEST_CHECKLIST.md))

* C. Software Design Description
  See [software_design_document.md](../design/software_design_document.md); module layout and request path:
  [directory_layout.md](../directory_layout.md)

* D. User and Installation Guide
  See [user_guide.md](../user/user_guide.md)

* E. Benchmark and Evaluation Report
  See [benchmark_report.md](../evaluation/benchmark_report.md)
