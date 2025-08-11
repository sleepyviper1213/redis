# Software Design Document (SDD)

<!-- TOC -->
* [Software Design Document (SDD)](#software-design-document-sdd)
  * [Document information](#document-information)
  * [1. Introduction](#1-introduction)
    * [1.1 Purpose](#11-purpose)
    * [1.2 Scope](#12-scope)
    * [1.3 Definitions, Acronyms, and Abbreviations](#13-definitions-acronyms-and-abbreviations)
    * [1.4 References](#14-references)
    * [1.5 Overview](#15-overview)
  * [2. Overall description](#2-overall-description)
    * [2.1 Design Goals and Constraints](#21-design-goals-and-constraints)
    * [2.2 Assumptions and Dependencies](#22-assumptions-and-dependencies)
  * [3. System architecture](#3-system-architecture)
    * [3.1 Design Decisions](#31-design-decisions)
    * [3.2 Alternative Choices](#32-alternative-choices)
    * [3.3 Trade-offs](#33-trade-offs)
    * [3.4 Implementation Notes](#34-implementation-notes)
  * [4. Data design](#4-data-design)
    * [4.1 Data Storage Model](#41-data-storage-model)
    * [4.2 Comparison with SQL Databases](#42-comparison-with-sql-databases)
  * [5. Interface](#5-interface)
  * [6. Components](#6-components)
    * [6.1 Command Execution Model](#61-command-execution-model)
    * [6.2 Protocol Parsing](#62-protocol-parsing)
      * [6.2.1 Design Decisions](#621-design-decisions)
      * [6.2.2 Alternatives Considered](#622-alternatives-considered)
      * [6.2.3 Trade-offs](#623-trade-offs)
      * [6.2.4 Implementation Notes](#624-implementation-notes)
    * [6.3 Storage Structure](#63-storage-structure)
    * [6.4 User Interface](#64-user-interface)
    * [6.5 In-Memory Store](#65-in-memory-store)
      * [6.5.1 Design Decisions](#651-design-decisions)
      * [6.5.2 Alternative Choices](#652-alternative-choices)
      * [6.5.3 Trade-offs](#653-trade-offs)
      * [6.5.4 Implementation Notes](#654-implementation-notes)
    * [6.6 Snapshot Manager](#66-snapshot-manager)
      * [6.6.1 Design Decisions](#661-design-decisions)
      * [6.6.2 Alternative Choices](#662-alternative-choices)
      * [6.6.3 Trade-offs](#663-trade-offs)
      * [6.6.4 Implementation Notes](#664-implementation-notes)
  * [7. Object-Oriented Models and Diagrams](#7-object-oriented-models-and-diagrams)
    * [7.1 Sequence Diagram](#71-sequence-diagram)
    * [7.2 State Diagrams](#72-state-diagrams)
    * [7.3 Deployment Architecture](#73-deployment-architecture)
  * [8. Performance Considerations](#8-performance-considerations)
<!-- TOC -->

---
## Document information

| Date       | Revision | Author               |
|------------|----------|----------------------|
| 6 Aug 2025 | 1        | Khac Truong Nguyen   |

## 1. Introduction
### 1.1 Purpose
This document describes the software design for the Redis-like server project. It outlines the architecture, design decisions, data structures, and modules used to implement the system based on the requirements defined in the SRS.

### 1.2 Scope
The system is an in-memory key-value store supporting basic Redis-like commands (`GET`, `SET`, `DEL`, etc.) over a custom TCP-based protocol. It includes support for persistence (snapshotting), client concurrency, and error handling.

### 1.3 Definitions, Acronyms, and Abbreviations

| Term       | Definition                                                       |
|------------|------------------------------------------------------------------|
| RESP       | Redis Serialization Protocol                                     |
| SRS        | Software Requirements Specification                              |
| SDD        | Software Design Document                                         |
| Snapshot   | A point-in-time binary dump of the in-memory store to disk       |
| Event Loop | A programming construct for handling asynchronous I/O operations |

### 1.4 References
- Software Requirement Specification (SRS)
- RESP Protocol Spec: https://redis.io/docs/reference/protocol-spec/
- Atlassian SDD Guidance: https://www.atlassian.com/work-management/knowledge-sharing/documentation/software-design-document

### 1.5 Overview
Section 2 provides a high-level overview. Section 3 details architectural decisions. Section 4 covers data design. Section 5 lists public interfaces. Section 6 describes components. Section 7 includes diagrams. Section 8 summarizes performance considerations.

## 2. Overall description

### 2.1 Design Goals and Constraints
- High throughput and low latency
- Minimal external dependencies
- Scalable command execution pipeline
- Single-threaded core with an event-loop architecture
- Simple RESP protocol parsing
- Optional persistence via RDB-style snapshots

### 2.2 Assumptions and Dependencies

- Assumptions:
  - Clients use RESP as defined in the Redis specification (SRS Section 2.5).
  - The system runs on a POSIX-compliant OS with sufficient memory (SRS Section 3.1.3).
  - Initial implementation supports string-based key-value pairs; complex types may be added later (SRS Section 1.2).
  - Network conditions are typical for TCP; extreme packet loss is out of scope.
  - Snapshot restoration assumes a valid `redis.snap`; corrupted files are logged and skipped (FR-9).

- Dependencies:
  - Boost.Asio: asynchronous TCP networking for non-blocking I/O and concurrent client handling.
  - cereal: binary serialization for snapshots.
  - spdlog: logging for events and errors.
  - C++23 Standard Library: core data structures and utilities.

---

## 3. System architecture

> Include a Component Diagram showing major subsystems/modules.

The system comprises:
- TCP Server: Accepts client connections
- RESP Parser: Parses incoming commands
- Command Dispatcher: Routes to appropriate handler
- Key-Value Store: In-memory data structure
- Snapshot Manager: Persistence
- Logger: Event tracking

### 3.1 Design Decisions
1. Client/Server Architecture
   - Standalone server accepting TCP connections from clients.
   - Rationale: Meets SRS client/server constraint, supports concurrent clients, and aligns with Redis-like architecture.

2. Layered Component Structure
   - Layers: networking (Server), command processing (Parser/Dispatcher), data management (KeyValueStore), persistence (Snapshot).
   - Rationale: Improves modularity, maintainability, and extensibility.

3. Asynchronous I/O with Boost.Asio
   - Non-blocking TCP communication handling multiple clients.
   - Rationale: Throughput and scalability for concurrent connections.

4. Event-Driven Command Processing
   - Pipeline: receive → parse → validate → execute → respond.
   - Rationale: Low latency and clear flow.

5. Single-Threaded Core with Worker Threads
   - Core loop for parsing/execution; background worker(s) for snapshotting/monitoring.
   - Rationale: Simplicity and correctness while avoiding blocking for ancillary tasks.

6. Modular Directory Layout
   - Directories: network/, command/, storage/, core/.
   - Rationale: Separation of concerns and maintainability.

### 3.2 Alternative Choices
- Multi-Threaded Server
  - Alternative: per-connection threads or a thread pool for execution.
  - Rejected: added synchronization complexity and risk of concurrency bugs; event loop meets targets.

- Multi-Process Model
  - Alternative: fork per client or subsystem.
  - Rejected: higher memory overhead and IPC complexity.

- Text-Based Persistence
  - Alternative: JSON snapshots.
  - Rejected: larger files and slower I/O; binary is faster and simpler.

- Third-Party RESP Parser
  - Alternative: reuse an external library.
  - Rejected: adds dependencies; reduces educational value.

### 3.3 Trade-offs
- Pros:
  - RESP compatibility and client/server model.
  - Layered structure eases extension and debugging.
  - Event-driven, non-blocking I/O supports concurrency.
  - Single-threaded core reduces complexity.

- Cons:
  - Single-threaded core can limit scalability under extreme load.
  - Blocking snapshot saves may temporarily affect availability, though mitigated by background workers.
  - Modular setup adds initial structure overhead.

### 3.4 Implementation Notes
- Technology: C++23, Boost.Asio, cereal, spdlog.
- Error Handling: Structured RESP errors for invalid commands and I/O issues; all errors logged.
- Scalability: Asio I/O context serves multiple connections; offload non-critical tasks to worker thread(s).
- Future Work: Background snapshot saving, AOF persistence, clustering.

---

## 4. Data design

The design favors a simple, high-performance key-value model aligned with NoSQL systems, differing from relational SQL databases.

### 4.1 Data Storage Model
- Structure: `std::unordered_map<std::string, std::string>` stores key-value pairs in memory.
- Justification: O(1) average-case complexity for `GET`, `SET`, and `DEL`, enabling low latency.
- Access: Protected by a mutex to ensure thread safety with concurrent clients; read/write granularity may be improved later.
- Size Tracking: Approximate memory usage for an `INFO`-like command.

### 4.2 Comparison with SQL Databases
- Data Structure: Hash map vs. normalized tables
- Schema: Schema-less vs. fixed schema
- Access Complexity: O(1) average vs. index/table-scan and joins
- Data Relationships: Flat, independent keys vs. relational integrity
- Persistence: Optional binary snapshots vs. on-disk ACID storage
- Query Language: Simple commands (RESP) vs. SQL
- Concurrency: Event-driven, single-threaded core vs. multi-thread/process with isolation levels
- Use Case: Low-latency key-value operations vs. complex queries and relationships

Rationale:
- Simplicity: No normalization or schema design required.
- Performance: In-memory O(1) aligns with throughput targets.
- Flexibility: Dynamic data without predefined schemas.
- Trade-offs: No joins or relational querying; acceptable given scope.

---

## 5. Interface

| Module            | Description                | Public Methods                  |
|-------------------|----------------------------|---------------------------------|
| TcpServer         | Accepts client connections | run(), stop()                   |
| RespParser        | Parses RESP format         | parse(buffer)                   |
| CommandExecutor   | Executes commands          | execute(command)                |
| KeyValueStore     | In-memory map              | get(), set(), del()             |
| SnapshotManager   | Persists DB state          | save(), load()                  |

---

## 6. Components

### 6.1 Command Execution Model
- Choice: Single-threaded event loop using Boost.Asio
- Alternatives: Per-connection threads; thread pool for command execution
- Pros: Simplicity, minimal concurrency bugs, low memory overhead
- Cons: Potential CPU underutilization on multi-core systems
- Implementation Notes:
  - Event Loop: Asio `io_context` handles connections, parsing, and dispatch.
  - Command Pipeline: Sequential per client; fairness and simplicity.
  - Error Handling: RESP error strings for invalid usage; all errors logged.
  - Future Work: Consider a small thread pool for heavier commands.

### 6.2 Protocol Parsing

#### 6.2.1 Design Decisions
1. Custom RESP Parser
   - C++ parser for RESP’s ASCII, CRLF-terminated format.
   - Rationale: Avoid extra deps and improve transparency for learning.

2. State Machine Approach
   - Finite state machine for incremental parsing of arrays, bulk strings, and arguments.
   - Rationale: Robust against malformed inputs and efficient for streaming.

3. Command Registry
   - Map from command names (e.g., SET, GET) to handlers.
   - Rationale: Extensible and simple dispatch.

4. Error Handling
   - Structured RESP errors for invalid commands and argument counts.
   - Rationale: Reliability and clear client feedback.

#### 6.2.2 Alternatives Considered
- Third-Party Parser: Faster to integrate, but adds dependencies and reduces educational value.
- Recursive Descent: Intuitive but higher memory usage and risk on deep inputs.
- Regular Expressions: Simple, but not suitable for streaming and large inputs.

#### 6.2.3 Trade-offs
- Pros: Control, efficiency, extensibility
- Cons: More development effort; complexity grows with protocol features

#### 6.2.4 Implementation Notes
- Technology: C++23, Boost.Asio
- Error Cases: Malformed RESP (e.g., missing CRLF), unknown commands; all logged
- Future Work: Hooks for more complex commands

### 6.3 Storage Structure
- Choice: `std::unordered_map<std::string, std::string>`
- Pros: O(1) average access; straightforward serialization
- Cons: No native expiry; no complex data types
- Alternatives:
  - `std::map`: ordered but slower operations
  - Custom Hash Table: full control but significant effort and risk
  - DB Backend (e.g., SQLite): persistence/queries but out of scope
- Implementation Notes:
  - Mutex protects access; consider finer-grained or lock-free in the future
  - Potential addition: expiry via secondary structure (e.g., min-heap by TTL)

### 6.4 User Interface
- Protocol-based server; no GUI
- Client Interaction: RESP-formatted commands over TCP; responses also RESP-formatted
- Command-Line Configuration: Flags for port and snapshot interval
- Logging: Console/file via spdlog with configurable verbosity
- Monitoring: `INFO`-like command for runtime stats
- Future Work: Admin CLI; RESP3 enhancements

### 6.5 In-Memory Store

#### 6.5.1 Design Decisions
- Data Structure: `std::unordered_map<std::string, std::string>`
- String-Only Support: Simplifies implementation and aligns with scope
- Thread Safety: Mutex-based protection for concurrent access
- Memory Management: Approximate usage tracking for monitoring

#### 6.5.2 Alternative Choices
- `std::map`: ordered iteration vs. slower ops
- Custom Allocator: potential perf gains vs. complexity
- Embedded KV DB: advanced features vs. scope/dependencies

#### 6.5.3 Trade-offs
- Pros: Fast, standard, simple; mutex ensures correctness
- Cons: Possible contention; limited data types; approximate memory metrics

#### 6.5.4 Implementation Notes
- Technology: C++23 standard library
- Error Cases: Return nil-like semantics for missing keys; operations logged
- Future Work: Additional data types; improved concurrency models

### 6.6 Snapshot Manager

#### 6.6.1 Design Decisions
- RDB-like Format: Binary snapshot file (e.g., `redis.snap`)
- Backup Mechanism: Rotate to `redis.snap.bak` before writing
- Blocking Save: Simple and consistent; may be offloaded later
- Startup Restoration: Load snapshot if present

#### 6.6.2 Alternative Choices
- AOF: Incremental writes vs. larger file and slower restore
- Text-Based Serialization: Human-readable vs. larger and slower
- Database Backend: Robustness vs. dependency and scope mismatch

#### 6.6.3 Trade-offs
- Pros: Simple, reliable, educational; backup enhances safety
- Cons: Blocking save can impact availability; binary is not human-readable

#### 6.6.4 Implementation Notes
- Technology: C++23 with cereal for serialization
- Error Cases: I/O errors lead to structured responses and logs
- Future Work: Background saving; optional AOF

---

## 7. Object-Oriented Models and Diagrams

### 7.1 Sequence Diagram
> Include a sequence diagram of a SET command from client to response.

### 7.2 State Diagrams
> Include a state diagram for the client connection lifecycle (Idle → Connected → Authenticated → Parsing → Closed).

### 7.3 Deployment Architecture
> Include a deployment diagram (server on Linux, multiple TCP clients, optional persistent volume).

---

## 8. Performance Considerations
- O(1) access time with hash map for typical operations
- Minimize heap allocations in parser; reuse buffers for I/O
- Efficient logging levels to avoid overhead in hot paths
- Snapshot writes may be moved to a background thread for reduced latency impact
