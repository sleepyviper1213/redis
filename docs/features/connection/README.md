# Feature: Connection

| Item         | Value |
|--------------|-------|
| Feature ID   | FEAT-CONNECTION |
| Status       | Implemented; documented retroactively |
| Requirements | FR-008, FR-009, FR-014 (defined in the [SRS](../../analysis/software_requirement_specification.md); not repeated here) |
| Issue / epic | TODO |

Accepting TCP clients, the RESP protocol, limits per client.

## Use cases

| Use case | Actor | Commands / trigger | Requirements |
|----------|-------|--------------------|--------------|
| [UC-005 Client connection ping-pong](use_cases/uc_005_ping.md) | Client | `PING`, `ECHO` | FR-007, FR-014 |
| [UC-006 Connect over TCP](use_cases/uc_006_connect.md) | Client | TCP connect, RESP2 or inline requests | FR-008, FR-009, FR-014 |

## Diagrams

* Connection lifecycle: [state_connection_lifecycle.puml](diagrams/state_connection_lifecycle.puml)
* `PING` and connection limits: [sequence_connection_ping_pong.puml](diagrams/sequence_connection_ping_pong.puml)
* Client-server interaction: [sequence_client_server_interaction.puml](diagrams/sequence_client_server_interaction.puml)
* Threads and processes: [concurrency_threads.puml](diagrams/concurrency_threads.puml)
* One request, then shutdown, with several I/O threads: [sequence_threads_request_and_shutdown.puml](diagrams/sequence_threads_request_and_shutdown.puml)

## Documents in this folder

| Document | Content |
|----------|---------|
| [test_plan.md](test_plan.md) | Test scenarios `TC-nn` |
| [traceability.md](traceability.md) | Use cases to requirements, scenarios and tests |
| [design.md](design.md) | Design choices `DX-n`, code map, open questions |
| [io_uring_experiment.md](io_uring_experiment.md) | Profile a request's cost, then try Asio's io_uring backend (TC-298) |
| [adr/](adr/) | Decisions: ADR-0001 to 0003 |
