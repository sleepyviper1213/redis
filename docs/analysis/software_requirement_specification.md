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
    * [3.5.7 Sequence Diagram](#357-sequence-diagram)
* [4. Appendix](#4-appendix)
<!-- TOC -->

---

# Document information

| Date       | Revision | Author             |
|------------|----------|--------------------|
| 7 Aug 2025 | 3        | Khac Truong Nguyen |

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
| GUI          | Graphical user interface              |


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

### 3.2.1 Standard Compliance

The system must comply with the following technical and platform standards:

* **RESP Protocol Specification**
  The Redis-like server shall fully comply with the **RESP version 2** protocol, ensuring compatibility with standard Redis clients. This includes correct formatting of bulk strings, arrays, errors, and integers.

* **ISO C++ Standards**
  The implementation shall follow **ISO/IEC 14882:2024 (C++23)** or later. Code must avoid undefined behavior and adhere to safe memory and concurrency practices.

* **POSIX Compliance**
  File I/O, process signals, and error handling shall be compatible with **POSIX** to support deployment on Unix-based systems.

* **Filesystem Access**
  Filesystem operations shall use compatible abstraction for cross-platform snapshot storage.
* No **external database** or file-based storage engine other than snapshots
* No use of Redis source code; all components must be implemented from scratch.

### 3.2.2 Accounting & Auditing Procedures

The server shall implement logging and traceability mechanisms to support debugging, usage analytics, and post-mortem analysis.

* **Command Logging**
  All incoming commands (excluding volatile or noisy ones like `PING`) shall be logged with:

  * Timestamp
  * Client connection identifier (IP, port)
  * Command name and sanitized parameters
  * Execution result (success/failure)

* **Snapshot Logging**
  Each snapshot event must be logged with:

  * Snapshot trigger source (manual, timer, threshold)
  * Number of keys serialized
  * Output file path
  * Timestamp and hash digest (e.g., SHA-256)

* **Authentication Logging**
  All authentication attempts (successful or failed) must include:

  * Client IP
  * Time of attempt
  * Status (OK/Fail)
  * Lockout flag if repeated failures exceed threshold

* **Retention & Archiving**
  All logs shall be retained for a minimum of **30 days**. Old logs may be archived (e.g., GZIP-compressed) automatically based on configurable policy.

* **Audit Readability**
  Log files shall be stored in a **human-readable** format (e.g., newline-delimited JSON or plain text) for ease of inspection.

---

### 3.2.3 Legal & Data Privacy Constraints

* **User Data Protection**
  Any sensitive data (e.g., authentication credentials) must never be logged in plaintext. Authentication data should be handled in memory only and never persisted to disk.

* **Third-party Library Licensing**
  All third-party libraries (e.g., Boost, nlohmann/json) must be used in compliance with their respective licenses (e.g., MIT, Boost Software License 1.0). The project must not integrate non-permissive or GPL-incompatible dependencies without explicit approval.


### 3.2.4 Resource Constraints

* The server must be lightweight and runnable on machines with:

  * CPU: 2-core minimum
  * RAM: ≤ 128 MB for small datasets
  * Disk: Must not write more than 1 snapshot per 5 seconds

---



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

Thanks for sharing your diagrams! Based on your uploaded `.puml` files, here’s a **completed and well-written section 3.5 – Object Oriented Models** for your Software Requirements Specification (SRS):

---

## 3.5 Object-Oriented Models

This section presents the design models that describe the structure, behavior, and deployment of the Redis-like server system using object-oriented analysis. Each diagram file is maintained in the `diagrams/` folder and written using PlantUML syntax.

---

### 3.5.1 Architecture Specification

The overall component-based system architecture is defined in:

 [`components.puml`](diagrams/components.puml)

This diagram describes major system components, including:

* Command Parser
* In-Memory Store
* Snapshot Manager
* File System
* TCP Listener
* Admin and Client actors

It shows how responsibilities are separated and how components interact over TCP and file I/O.

---

### 3.5.2 Data Flow Diagrams

The system’s data flow is modeled using three levels:

* [`dfd_context.puml`](diagrams/dfd_context.puml)
  Provides a high-level context of external entities (Client, File System) and their data exchanges with the system.

* [`dfd_execute.puml`](diagrams/dfd_execute.puml)
  Focuses on how commands flow through parsing, dispatching, execution, and storage.

* [`dfd_subprocess.puml`](diagrams/dfd_subprocess.puml)
  Breaks down the snapshot subprocess into data interactions between the in-memory store and the file system.

---

### 3.5.3 State Diagram

* [`state_connection_lifecycle.puml`](diagrams/state_connection_lifecycle.puml)

This state diagram models the lifecycle of a client connection, from TCP handshake through authentication, command parsing, execution, and connection closure. It helps identify valid transitions and error conditions in network sessions.

---

### 3.5.4 Deployment Diagram

* [`deployment.puml`](diagrams/deployment.puml)

This diagram shows how the Redis-like server is deployed in a real-world environment. It includes:

* Server binary running on Linux
* Snapshot storage on disk
* External clients connecting over TCP
* Optional admin tools

It is helpful to understand infrastructure and networking setup.

---

### 3.5.5 Activity Diagrams

* [`activity_command_execution_pipeline.puml`](diagrams/activity_command_execution_pipeline.puml)
  Describes the internal flow from when a command is received to when the result is sent back.

* [`activity_startup.puml`](diagrams/activity_startup.puml)
  Models the startup sequence: loading configuration, initializing components, opening sockets, etc.

These diagrams emphasize internal operational logic and system behavior.

---

### 3.5.6 Use Case Diagram

* [`use_case.puml`](diagrams/use_case.puml)

This diagram models the primary use cases for both **client** and **admin** actors, including:

* Store and retrieve key-value pairs
* Configure settings (admin)
* Trigger snapshots
* Optional authentication
* Extensibility hooks for future features like Pub/Sub


---

### 3.5.7 Sequence Diagram

* [`sequence_client_server_interaction.puml`](diagrams/sequence_client_server_interaction.puml)

This diagram represents a full client-server interaction sequence:

* TCP connection established
* RESP command sent
* Parsed and dispatched
* Response returned

It is useful for visualizing protocol flow and latency hotspots.

---

# 4. Appendix

* A. Use Case Description
  Use case descriptions are maintained in a separate document for clarity and modularity. See [use_case_descriptions.md](use_case_descriptions.md)

* B. Test Plan Checklist
  See [TEST_CHECKLIST.md](../test/TEST_CHECKLIST.md)
