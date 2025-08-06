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
  * [TODO](#todo)
* [3. Specific Requirements](#3-specific-requirements)
  * [3.1 External interface Requirements](#31-external-interface-requirements)
    * [3.1.1 User Interfaces](#311-user-interfaces)
    * [3.1.2 Hardware Interfaces](#312-hardware-interfaces)
    * [3.1.3 Software interfaces](#313-software-interfaces)
    * [3.1.4 Communication interfaces](#314-communication-interfaces)
  * [3.2 Design Constraints](#32-design-constraints)
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
  * [3.5 Object Oriented Models](#35-object-oriented-models)
    * [3.5.1 Architecture specification](#351-architecture-specification)
    * [3.5.2 Class diagram](#352-class-diagram)
    * [3.5.3 State diagram](#353-state-diagram)
    * [3.5.4 Collaboration diagram](#354-collaboration-diagram)
    * [3.5.1 Activity diagram](#351-activity-diagram)
* [4. Appendix](#4-appendix)
<!-- TOC -->

---

# Document information

| Date       | Revision | Author             |
|------------|----------|--------------------|
| 6 Aug 2025 | 2        | Khac Truong Nguyen |

---
# 1. Introduction

## 1.1 Purpose

This document specifies the software requirements for a custom-built Redis-compatible in-memory data store developed in
C++ using Boost.Asio and a custom RESP protocol parser. It targets educational use, experimentation, and potential
optimization studies.

## 1.2 Scope

### 1.2.1 In Scope

* Core Redis commands (`SET`, `GET`, `DEL`, `EXISTS`, etc.).
* Custom RESP command parser.
* Snapshot persistence to disk (RDB-like).
* Simple client-server model using TCP sockets via Boost.Asio.
* Sequence of logs for testing and debugging.

### 1.2.2 Out of Scope

* Advanced Redis features such as clustering, pub/sub, Lua scripting.
* Full Redis protocol compatibility (e.g., RESP3).
* GUI or external tools integration.

## 1.3 Definitions, Acronyms, and Abbreviations

| Term         | Definition                            |
|--------------|---------------------------------------|
| RESP         | Redis Serialization Protocol          |
| RDB          | Redis Database file (snapshot format) |
| SRS          | Software Requirements Specification   |
| HTTP         | HyperText Transfer Protocol           |
| CLI          | Command Line Interface                |
| FR           | Functional Requirement                |
| NFR          | Non-functional Requirement            |
| Snapshotting |                                       |
| CRLF         |                                       |
| GUI| Graphical user interface|


## 1.4 References

* Redis Documentation: https://redis.io/docs/
* IEEE 830-1998: Recommended Practice for Software Requirements Specifications
* Boost.Asio: https://www.boost.org/doc/libs/release/doc/html/boost_asio.html
* Build Redis from scratch: https://build-your-own.org/redis/

## 1.5 Overview

Section 2 provides a high-level overview, while Section 3 details functional and non-functional requirements. Section 4
includes appendices and supporting diagrams.

---

# 2. Overall Description

## 2.1 Product Perspective

### 2.1.1 Business Case and Operational Concept

This project aims to develop a lightweight, high-performance in-memory key-value data store, inspired by the Redis
architecture. The system is designed to support a subset of the Redis protocol with core functionalities such as `GET`,
`SET`, `DEL`, and snapshotting through a custom RESP command parser. The primary use case is to support educational,
research, and prototyping environments where a simplified Redis server can be deployed and extended easily.

The operational concept centers around a server application that listens for commands over a TCP or HTTP interface.
Users, either via scripts or client applications, send commands to store, retrieve, or delete key-value data. The system
ensures persistence through periodic snapshots (RDB-like) saved to disk.

This project also serves as a hands-on learning platform for exploring system-level programming, concurrent I/O with
Boost.Asio, memory management, and software architecture for high-performance servers.

### 2.1.2 System Context and Fit

The proposed system fits into the broader context of distributed systems and cloud-native applications where
lightweight, embeddable, and portable in-memory storage components are valuable. While not intended to replace
production-grade systems like Redis, it can be integrated into:

* Development environments where Redis is too heavyweight or not customizable
* Embedded systems with limited compute where a minimal footprint is necessary
* Educational use cases in operating systems, networking, or database systems courses

It operates independently but may interface with monitoring tools or lightweight web dashboards. In larger systems, it
may serve as a local cache or volatile storage component.

## 2.2 Product Functions
The system provides the following high-level functionalities:

| **ID** | **Requirement**                       | **Description**                                                                             | **Priority** |
|--------|---------------------------------------|---------------------------------------------------------------------------------------------|--------------|
| BR-001 | Fast Key-Value Data Storage           | The system must allow clients to store and retrieve key-value data with minimal latency.    | High         |
| BR-002 | In-Memory Performance                 | Data must be kept in memory to ensure real-time speed for read/write operations.            | High         |
| BR-003 | Command-Based Interface               | Users must interact with the server using a simple text-based command protocol (like RESP). | High         |
| BR-004 | Snapshot Persistence                  | The system must support snapshotting to persist data to disk at specific intervals.         | Medium       |
| BR-005 | Basic Data Types Support              | Support must be provided for common data types: strings, lists, sets, hashes.               | Medium       |
| BR-006 | Multi-Client Access                   | The system must handle multiple concurrent client connections via TCP.                      | High         |
| BR-007 | Minimal Resource Footprint            | The solution should be lightweight and suitable for constrained environments.               | Medium       |
| BR-008 | Configurable Settings                 | Admins should be able to configure server parameters (e.g., port, persistence frequency).   | Low          |
| BR-009 | Extensibility for Additional Features | The system design must support adding new features such as Pub/Sub or replication later.    | Medium       |
| BR-010 | Cross-Platform Compatibility          | The server should be buildable and runnable on major OS platforms (Linux, macOS).           | Low          |

Diagram: Use Case Diagram (use_case.puml)
## 2.3 User Characteristics

| User Type | Characteristics                                      |
|-----------|------------------------------------------------------|
| Developer | C++ programmers testing key-value systems            |
| Tester    | Uses Python clients or Redis-CLI to test GET/SET/DEL |
| Student   | Learns about protocol parsing, async IO              |

## 2.4 Constraints

* **Memory Constraints**: The system maintains all key-value pairs in memory. As such, the maximum dataset size is
  limited by available RAM. No paging or overflow mechanism is implemented in this version.
* **Operational Constraints**:
  * Single-node operation: This version does not support clustering or replication.
  * Snapshot creation may temporarily pause command processing depending on implementation.
  * The system assumes a reliable filesystem for persistence. Unexpected shutdowns between snapshots may lead to data
    loss.
  * Interface to be used with the Internet.
  * Servers must be able to serve queries concurrently.
  * The system must maintain 99.9% uptime to support 24/7 user access.
  * The system will be designed according to a client/server model.

* **Site Adaptation Constraints**:

  * The system is developed and tested on POSIX-compliant systems (e.g., Linux, macOS). Windows is not supported.
  * Adaptation to constrained environments (e.g., IoT or embedded Linux) may require recompilation and fine-tuning of
    memory usage.
  * No built-in support for container orchestration (e.g., Kubernetes) is provided but can be adapted via custom
    deployment scripts.

## 2.5 Assumptions and Dependencies

* All commands are ASCII-based and terminated by CRLF
* Users will not send binary payloads
* Server runs on localhost for development and testing with sufficient disk space (>1GB).

## 2.6 Risk Analysis

This section identifies potential risks in developing and operating the Redis-like in-memory data store, their impacts,
likelihood, and mitigation strategies to ensure project success.

| Risk ID | Risk Description                                             | Impact                                                          | Likelihood | Mitigation Strategy                                                                                                                                    |
|---------|--------------------------------------------------------------|-----------------------------------------------------------------|------------|--------------------------------------------------------------------------------------------------------------------------------------------------------|
| R-001   | Snapshot file corruption during save or load                 | High: Data loss or inability to restore database                | Low        | Implement checksums (e.g., CRC32) for snapshot validation; log errors and revert to `redis.snap.bak` if corruption detected (FR-007, FR-008, NFR-003). |
| R-002   | Boost.Asio dependency issues (e.g., version incompatibility) | Medium: Build failures or portability issues across macOS/Linux | Medium     | Use a fixed Boost version (e.g., 1.85); document fallback libraries (e.g., standard C++ networking); test on target platforms (NFR-007).               |
| R-003   | Performance bottlenecks under concurrent connections         | High: Failure to meet ≤1ms GET/SET latency (NFR-001)            | Medium     | Optimize command parser and database store; use asynchronous I/O with Boost.Asio; benchmark with tools like JMeter (FR-016, planned).                  |
| R-004   | Malformed RESP commands causing server crashes               | High: System instability or downtime                            | Low        | Implement robust command validation and error handling; reject malformed commands with structured errors (FR-006, FR-013, NFR-002).                    |
| R-005   | Inadequate error logging for debugging                       | Medium: Difficulty diagnosing issues during testing             | Low        | Ensure comprehensive logging of commands, errors, and metrics using spdlog; verify logs in test scenarios (FR-004, FR-012, NFR-003).                   |
| R-006   | Limited developer expertise in C++23 or Boost.Asio           | Medium: Delays in implementation or poor code quality           | Medium     | Provide training resources; use modular design for easier onboarding; leverage online documentation (NFR-006).                                         |
| R-007   | Snapshot save blocking client operations                     | High: Temporary unavailability during SAVE (NFR-005)            | High       | Plan for background snapshotting in future iterations; minimize blocking time with efficient serialization (FR-007, NFR-001).                          |
| R-008   | Unhandled client input                                       | Medium: May crash the server                                    | High       | Add robust input validation and logging                                                                                                                |

- Risks are prioritized based on impact and likelihood, with high-impact risks (e.g., R-001, R-003, R-004, R-007)
  addressed in core requirements (FR-001, FR-006, FR-007, NFR-001, NFR-003).
- Mitigation strategies leverage existing functional and non-functional requirements where possible.
- Future enhancements (e.g., background snapshotting, FR-016) may reduce risks like R-007.

## 2.7 Acceptance Criteria

TODO
- 
---

# 3. Specific Requirements

## 3.1 External interface Requirements

### 3.1.1 User Interfaces

* Command-line interface via Python client or HTTP tools

### 3.1.2 Hardware Interfaces

* None required; runs on general-purpose CPU with 512MB+ RAM

### 3.1.3 Software interfaces

* Operating systems: macOS, Linux
* Compiler: Clang++, GCC 14+
* Build System: CMake 3.20+
* Dependencies:
    * Boost.Asio for networking
    * cereal for serialisation/deserialisation
    * spdlog for logging

### 3.1.4 Communication interfaces

* TCP-based RESP communication

Diagram: Client Connection State Machine

The state machine diagram describes the lifecycle of a client connection, transitioning from Disconnected to Connected, Authenticated (if required), Processing, and Closed states. It captures events like TCP connection acceptance, authentication success/failure, and command processing.

[See state_connection_lifecycle.puml in Appendix B for the diagram.]
Diagram: Sequence Diagram for Client-Server Interaction

The sequence diagram illustrates a client-server interaction, showing a client connecting via TCP, sending a RESP command (e.g., GET key), the server parsing and executing the command, and returning a response (e.g., value). It also shows connection cleanup on disconnect.

[See sequence_client_server_interaction.puml in Appendix B for the diagram.]

## 3.2 Design Constraints
* **Language**: C++23
* **Networking**: Boost.Asio
* **Protocol**: RESP (custom parser)
* **Persistence**: Snapshot via RDB-like format
* **Client**: Python script or RESP-compatible tool
* No **external database** or file-based storage engine other than snapshots
* No use of Redis source code; all components must be implemented from scratch.
* Must work on macOS and Linux.

## 3.3 Functional Requirements

| ID     | Requirement                          | Description                                                         | Priority |
|--------|--------------------------------------|---------------------------------------------------------------------|----------|
| FR-01  | Execute Redis commands               | Support GET, SET, DEL for key-value operations                      | High     |
| FR-02  | TCP server setup                     | Establish TCP listener for client connections                       | High     |
| FR-03  | Configuration loading                | Load settings (e.g., port, snapshot interval) from config file      | Low      |
| FR-04  | Logging system initialization        | Set up spdlog for command and error logging                         | High     |
| FR-05  | INFO                                 | Return statistics such as uptime, number of keys, and memory usage. | Low      |
| FR-06  | Invalid Command Handling             | Detect and reject unsupported or malformed commands.                | Low      |
| FR-07  | Snapshot File                        | Save all key-value pairs to a file (`redis.snap`).                  | Low      |
| FR-08  | Backup on Save                       | Rename previous snapshot to `redis.snap.bak` before overwriting.    | Low      |
| FR-09  | Snapshot restoration                 | Reload `redis.snap` on if present.                                  | Low      |
| FR-011 | Concurrent Clients                   | Support multiple simultaneous HTTP client connections.              | Low      |
| FR-012 | Log Requests                         | Log command execution, client IP, and response time.                | Low      |
| FR-013 | Error Responses                      | Return well-structured errors on bad input or system issues.        | Low      |
| FR-014 | Background monitoring                | Monitor system metrics (e.g., memory, connections) in background    | Low      |
| FR-016 | Benchmarking (Planned)               | Record throughput and latency under load.                           | Low      |
| FR-017 | Authentication or client permissions | Support client authentication (optional, planned)                   | Low      |
| FR-018 | Pub/Sub                              | Support publish/subscribe messaging (planned)                       | Low      |
| FR-019 | Transactions                         | Support multi-command transactions (planned)                        | Low      |
| FR-020 | Lua scripting                        | Support Lua script execution (planned)                              | Low      |
| FR-021 | Clustering or replication            | Support distributed setups (planned)                                | Low      |

Diagram: Command Execution Pipeline (command_execution_pipeline.puml)
This diagram outlines command processing: receiving a RESP request, parsing, executing, and responding. It supports
FR-01 and FR-06 by showing command validation and execution.
[See Appendix B for details.]

Diagram: Level 1 Data Flow Diagram (dfd_subprocess.puml)
This Level 1 DFD details processes: command parsing, validation, execution, and snapshot saving. It supports FR-01,
FR-07, and FR-09 by showing data flows.
[See Appendix B for details.]

Diagram: Level 2 DFD - Execute Command (dfd_execute.puml)
This Level 2 DFD focuses on GET, SET, and DEL execution, showing interactions with the in-memory store. It supports
FR-01.
[See Appendix B for details.]

Diagram: Startup Initialization (startup.puml)
This diagram shows the startup sequence: loading configuration, initializing subsystems, restoring snapshots, and
starting monitoring. It supports FR-02, FR-03, FR-09, and FR-014.
[See Appendix B for details.]
---

## 3.4 Nonfunctional Requirements

### [NFR-001] Performance

* GET/SET latency: ≤1ms under 1,00 concurrent connections
* Snapshot SAVE latency: ≤100ms blocking
* Throughput: ≥10,00 ops/sec on 4-core Apple Silicon CPU
* Cold start: ≤500ms with <10MB snapshot
* RESP parsing: <5ms per command
* Snapshot write: ≥1MB/s

### [NFR-002] Security

* Reject malformed/unknown commands
* Ensure snapshot backup (`redis.snap.bak`) before overwrite

### [NFR-003] Reliability

* Crash recovery via the latest snapshot
* Fail-safe snapshot rotation
* Log all commands and errors

### [NFR-004] Application Affinity and Compatibility

* Compatible with RESP test clients
* Potential Redis CLI compatibility via wrapper

### [NFR-005] Availability

* Remain operational during snapshot saves

### [NFR-006] Maintainability

* Modular structure: `command/`, `storage/`, `core/`
* Separate parser, logic, and transport layers

### [NFR-007] Portability

* Compile/run on macOS and Linux with CMake

### [NFR-008] Extensibility

* Modular design for new commands/features (e.g., AOF)
* Stubbed Pub/Sub module

### [NFR-009] Reusability

* Reusable modules: command parser, snapshot handler
* Self-contained logging, error, and HTTP layers

## 3.5 Object Oriented Models
### 3.5.1 Architecture specification
Check out [components.puml](diagrams/components.puml) for system architecture.

### 3.5.2 Class diagram
### 3.5.3 State diagram
### 3.5.4 Collaboration diagram
### 3.5.1 Activity diagram


# 4. Appendix

* A. Use Case Description
  Use case descriptions are maintained in a separate document for clarity and modularity. See [use_case_descriptions.md](use_case_descriptions.md)
* B. Diagrams
  1. **components.puml** (Section 2.1): Shows system components (Client Interface, Server, Command Parser,
     KeyValueStore, SnapshotEngine) with notes on roles.
  2. **dfd_context.puml** (Section 2.1): Level 0 DFD showing client-server and snapshot file interactions.
  3. **deployment.puml** (Section 2.1): Physical deployment of the server and clients.
  4. **use_case.puml** (Section 2.2): Client use cases for `SET`, `GET`, `DEL`, and `SAVE`.
  5. **state_connection_lifecycle.puml** (Section 3.1.4): Client connection state transitions.
  6. **sequence_client_server_interaction.puml** (Section 3.1.4): Client-server command interaction sequence.
  7. **command_execution_pipeline.puml** (Section 3.2): Command processing flow.
  8. **dfd_subprocess.puml** (Section 3.2): Level 1 DFD of system processes and data flows.
  9. **dfd_execute.puml** (Section 3.2): Level 2 DFD for command execution.
  10. **startup.puml** (Section 3.2): Server startup sequence.
* C. Test Plan Checklist
  See [TEST_CHECKLIST.md](../test/TEST_CHECKLIST.md)
