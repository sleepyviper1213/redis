# Software Requirements Specification (SRS)

# Table of Contents

<!-- TOC -->
* [Software Requirements Specification (SRS)](#software-requirements-specification-srs)
* [Table of Contents](#table-of-contents)
* [Document information](#document-information)
* [1. Introduction](#1-introduction)
  * [1.1 Purpose](#11-purpose)
  * [1.2 Scope](#12-scope)
  * [1.3 Definitions, Acronyms, and Abbreviations](#13-definitions-acronyms-and-abbreviations)
  * [1.4 References](#14-references)
  * [1.5 Overview](#15-overview)
* [2. Overall Description](#2-overall-description)
  * [2.1 Product Perspective](#21-product-perspective)
  * [2.2 Product Functions](#22-product-functions)
  * [2.3 User Characteristics](#23-user-characteristics)
  * [2.4 Constraints](#24-constraints)
    * [2.5 Assumptions and Dependencies](#25-assumptions-and-dependencies)
* [3. Specific Requirements](#3-specific-requirements)
  * [3.1 External interface Requirements](#31-external-interface-requirements)
    * [3.1.1 User Interfaces](#311-user-interfaces)
    * [3.1.2 Hardware Interfaces](#312-hardware-interfaces)
    * [3.1.3 Software interfaces](#313-software-interfaces)
    * [3.1.4 Communication interfaces](#314-communication-interfaces)
  * [3.2 Functional Requirements](#32-functional-requirements)
  * [3.3 Design Constraints](#33-design-constraints)
  * [3.4 Nonfunctional Requirements](#34-nonfunctional-requirements)
    * [3.4.1 Performance](#341-performance)
    * [3.4.2 Security](#342-security)
    * [3.4.3 Reliability](#343-reliability)
    * [3.4.4 Application Affinity and Compatibility](#344-application-affinity-and-compatibility)
    * [3.4.5 Availability](#345-availability)
    * [3.4.6 Maintainability](#346-maintainability)
    * [3.4.7 Portability](#347-portability)
    * [3.4.8 Extensibility](#348-extensibility)
    * [3.4.9 Reusability](#349-reusability)
* [4. Appendix](#4-appendix)
* [5. Index](#5-index)
<!-- TOC -->

---

# Document information

| Date       | Revision | Author             | Verified dep. Quality. |
|------------|----------|--------------------|------------------------|
| 1 Aug 2025 | 1        | Khac Truong Nguyen | No                     |

# 1. Introduction

## 1.1 Purpose

This document specifies the software requirements for a custom-built Redis-compatible in-memory data store developed in
C++ using Boost.Asio and a custom RESP protocol parser. It targets educational use, experimentation, and potential
optimization studies.

## 1.2 Scope

The software emulates core Redis functionalities including:

* In-memory key-value store
* `GET`/`SET`/`DEL` command support
* Snapshotting (RDB-like)
* HTTP interface for development/test integration
  This software is not intended for production use but for learning, analysis, and performance profiling.

## 1.3 Definitions, Acronyms, and Abbreviations

| Term | Definition                            |
|------|---------------------------------------|
| RESP | Redis Serialization Protocol          |
| RDB  | Redis Database file (snapshot format) |
| SRS  | Software Requirements Specification   |
| HTTP | HyperText Transfer Protocol           |
| CLI  | Command Line Interface                |
| FR   | Functional Requirement                |
| NFR  | Non-functional Requirement            |


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

This is a standalone server application, inspired by Redis. It is built from scratch in C++ and does not reuse Redis
source code. The system is composed of several components including the command parser, in-memory database engine, 
snapshot manager, and data storage. See [components.puml](diagrams/components.puml) for a component-level view of the system.

Diagram: Context-Level Data Flow Diagram (DFD)

The following Level 0 DFD illustrates the system’s interactions with external entities (clients) and the snapshot file. It shows clients sending RESP commands to the server, receiving RESP responses, and the server’s interaction with the snapshot file for saving and loading data on startup.

[See dfd_context.puml in Appendix B for the diagram.]
Diagram: Deployment Diagram

The deployment diagram depicts the physical architecture, showing client devices (e.g., Redis CLI, Python scripts) connecting via a TCP/IP network to a Linux server (e.g., Ubuntu 22.04). The server hosts the application layer (Redis-like server with Boost.Asio and custom RESP) and a storage layer with the snapshot file (redis.snap) mounted on a persistent volume.

[See deployment.puml in Appendix B for the diagram.]
## 2.2 Product Functions
The system provides the following high-level functionalities:
* Accept user commands via RESP protocol
* CRUD operations on string keys
* Persistence via snapshot (RDB)
* Simple logging and error reporting
  
The following use case diagram illustrates the primary interactions between users and system: [use_case.puml](diagrams/use_case.puml)
## 2.3 User Characteristics

| User Type | Characteristics                                      |
|-----------|------------------------------------------------------|
| Developer | C++ programmers testing key-value systems            |
| Tester    | Uses Python clients or Redis-CLI to test GET/SET/DEL |
| Student   | Learns about protocol parsing, async IO              |

## 2.4 Constraints

* Interface to be used with the Internet.
* Servers must be able to serve queries concurrently.
* The system will be designed according to a client/server model.
* The system must have a simple design and implementation.
* Focus on correctness and modularity over optimization


### 2.5 Assumptions and Dependencies

* All commands are ASCII-based and terminated by CRLF
* Users will not send binary payloads
* Server runs on localhost for development and testing

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

Diagram: Client Connection State Machine

The state machine diagram describes the lifecycle of a client connection, transitioning from Disconnected to Connected, Authenticated (if required), Processing, and Closed states. It captures events like TCP connection acceptance, authentication success/failure, and command processing.

[See state_connection_lifecycle.puml in Appendix B for the diagram.]
Diagram: Sequence Diagram for Client-Server Interaction

The sequence diagram illustrates a client-server interaction, showing a client connecting via TCP, sending a RESP command (e.g., GET key), the server parsing and executing the command, and returning a response (e.g., value). It also shows connection cleanup on disconnect.

[See sequence_client_server_interaction.puml in Appendix B for the diagram.]
## 3.2 Functional Requirements

| ID    | Requirement                          | Description                                                         |
|-------|--------------------------------------|---------------------------------------------------------------------|
| FR-1  | Execute Redis commands               | Support GET, SET, DEL for key-value operations                      |
| FR-2  | TCP server setup                     | Establish TCP listener for client connections                       |
| FR-3  | Configuration loading                | Load settings (e.g., port, snapshot interval) from config file      |
| FR-4  | Logging system initialization        | Set up spdlog for command and error logging                         |
| FR-5  | INFO                                 | Return statistics such as uptime, number of keys, and memory usage. |
| FR-6  | Invalid Command Handling             | Detect and reject unsupported or malformed commands.                |
| FR-7  | Snapshot File                        | Save all key-value pairs to a file (`redis.snap`).                  |
| FR-8  | Backup on Save                       | Rename previous snapshot to `redis.snap.bak` before overwriting.    |
| FR-9  | Snapshot restoration                 | Reload `redis.snap` on if present.                                  |
| FR-11 | Concurrent Clients                   | Support multiple simultaneous HTTP client connections.              |
| FR-12 | Log Requests                         | Log command execution, client IP, and response time.                |
| FR-13 | Error Responses                      | Return well-structured errors on bad input or system issues.        |
| FR-14 | Background monitoring                | Monitor system metrics (e.g., memory, connections) in background    |
| FR-16 | Benchmarking (Planned)               | Record throughput and latency under load.                           |
| FR-17 | Authentication or client permissions | Support client authentication (optional, planned)                   |
| FR-18 | Pub/Sub                              | Support publish/subscribe messaging (planned)                       |
| FR-19 | Transactions                         | Support multi-command transactions (planned)                        |
| FR-20 | Lua scripting                        | Support Lua script execution (planned)                              |
| FR-21 | Clustering or replication            | Support distributed setups (planned)                                |


See Appendix A for detailed use case descriptions.

---

## 3.3 Design Constraints
* **Language**: C++23
* **Networking**: Boost.Asio
* **Protocol**: RESP (custom parser)
* **Persistence**: Snapshot via RDB-like format
* **Client**: Python script or RESP-compatible tool
* No **external database** or file-based storage engine other than snapshots
* No use of Redis source code; all components must be implemented from scratch.
* Must work on macOS and Linux.

## 3.4 Nonfunctional Requirements

### 3.4.1 Performance

* GET/SET command latency: ≤ 1ms under 1,00 concurrent connections
* Snapshot SAVE latency: ≤ 100ms blocking during active operations
* Throughput: ≥ 10,000 ops/sec on 4-core Apple Silicon CPU
* Cold start time: ≤ 500ms with <10MB snapshot
* RESP parsing latency under 5ms per command
* Snapshot file write throughput: 1MB/s minimum

### 3.4.2 Security

* Rejects malformed or unknown commands
* Snapshot files are never overwritten without `.bak` backup

### 3.4.3 Reliability

* Recovers from a crash using the latest snapshot file
* Fail-safe snapshot file rotation using `.bak` scheme

* Logs all commands and errors

### 3.4.4 Application Affinity and Compatibility

* Compatible with RESP-compatible test clients
* May adapt to Redis CLI with thin wrapper

### 3.4.5 Availability

* Server should remain available during snapshot

### 3.4.6 Maintainability

* Modular directory layout: `command/`, `storage/`, `core/`, etc.
* Separation of parser, logic, and transport layers

### 3.4.7 Portability

* Must compile and run on macOS (ARM64/Intel) and Linux (x86\_64) with CMake

### 3.4.8 Extensibility

* Modular design allows for new commands, features (e.g., AOF)
* Pub/Sub module already stubbed in component model

### 3.4.9 Reusability

* Independent modules (e.g., command parser, snapshot handler) can be reused
* Logging, error system, and HTTP layer is self-contained

# 4. Appendix

* A. Use Case Description
  Use case descriptions are maintained in a separate document for clarity and modularity. See [use_case_descriptions.md](use_case_descriptions.md)
* B. Diagrams
  * Deployment Diagram ([deployment.puml](diagrams/deployment.puml)): Illustrates the physical deployment of the server on a Linux system, with client connections via TCP/IP and snapshot file storage.
  * Use Case Diagram ([use_case.puml](diagrams/use_case.puml)): Depicts client interactions with the system for SET, GET, DEL, and SAVE operations, with notes explaining each use case.
  * Client Connection State Machine ([state_connection_lifecycle.puml](diagrams/state_connection_lifecycle.puml)): Describes the states of a client connection, from Disconnected to Processing, including authentication and error handling.
  * Sequence Diagram for Client-Server Interaction (sequence_client_server_interaction.puml): Shows the sequence of a client sending a GET command, the server processing it, and returning a response.
  * Command Execution Pipeline (command_execution_pipeline.puml): Outlines the flow of processing a client’s RESP command, from parsing to response generation.
  * Context-Level DFD (dfd_context.puml): Shows the system’s interaction with clients and the snapshot file, highlighting RESP command/response flows and snapshot save/load operations.
  * Level 1 DFD (dfd_subprocess.puml): Breaks down the system into processes for command parsing, validation, execution, and snapshot saving, with data flows to the in-memory store and snapshot file.
  * Level 2 DFD - Execute Command (dfd_execute.puml): Details the execution of GET, SET, and DEL commands, showing interactions with the in-memory store.
  * Startup Initialization (startup.puml): Illustrates the server’s startup process, including configuration loading, subsystem initialization, and snapshot restoration.
* C. Test Plan Checklist
  See [TEST_CHECKLIST.md](../test/TEST_CHECKLIST.md)

# 5. Index
