# Feature: Server lifecycle

| Item         | Value |
|--------------|-------|
| Feature ID   | FEAT-SERVER_LIFECYCLE |
| Status       | Implemented; documented retroactively |
| Requirements | FR-013, FR-017, FR-018 (defined in the [SRS](../../analysis/software_requirement_specification.md); not repeated here) |
| Issue / epic | TODO |

Configuring, starting and stopping the server.

## Use cases

| Use case | Actor | Commands / trigger | Requirements |
|----------|-------|--------------------|--------------|
| [UC-016 Configure and start the server](use_cases/uc_016_configure_and_start.md) | Operator | Options, config file, `REDIS_*` environment | FR-013, FR-018 |
| [UC-017 Stop the server](use_cases/uc_017_stop_server.md) | Operator | `SIGINT`, `SIGTERM` | FR-017 |

## Diagrams

* Startup and shutdown: [activity_startup.puml](diagrams/activity_startup.puml)
* Stop on a signal: drain sessions, final save, exit status: [sequence_shutdown.puml](diagrams/sequence_shutdown.puml)

## Documents in this folder

| Document | Contents |
|----------|----------|
| [use_cases/](use_cases/) | The use cases of this feature |
| [test_plan.md](test_plan.md) | Test scenarios TC-nn, levels, coverage marks |
| [traceability.md](traceability.md) | Use case, requirement, scenario and test links, and how each NFR is verified |
| [design.md](design.md) | Design choices `DX-n`, code map, open questions |
| [adr/](adr/) | Decisions: ADR-0008 |
| [diagrams/](diagrams/) | PlantUML: startup activity and shutdown sequence |
