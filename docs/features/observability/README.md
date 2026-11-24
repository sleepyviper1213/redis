# Feature: Observability

| Item         | Value |
|--------------|-------|
| Feature ID   | FEAT-OBSERVABILITY |
| Status       | Implemented; documented retroactively |
| Requirements | FR-007, FR-015, FR-016 (defined in the [SRS](../../analysis/software_requirement_specification.md); not repeated here) |
| Issue / epic | #50 (jemalloc keyspace allocator); others TODO |

Inspecting the server and its logs at run time.

## Use cases

| Use case | Actor | Commands / trigger | Requirements |
|----------|-------|--------------------|--------------|
| [UC-014 Inspect the server](use_cases/uc_014_inspect_server.md) | Client | `INFO`, `CONFIG GET` | FR-007 |
| [UC-015 Change the log level](use_cases/uc_015_change_log_level.md) | Client | `CONFIG SET loglevel` | FR-007, FR-015 |
| [UC-018 Watch the logs](use_cases/uc_018_watch_logs.md) | Operator | Log file, watchdog warnings, crash reports | FR-015, FR-016 |

## Diagrams

* From a log call to the sinks, level change at run time, watchdog and crash report: [activity_logging_pipeline.puml](diagrams/activity_logging_pipeline.puml)

## Documents in this folder

| Document | Content |
|----------|---------|
| [test_plan.md](test_plan.md) | Test scenarios `TC-nn` |
| [traceability.md](traceability.md) | Use cases to requirements, scenarios and tests |
| [design.md](design.md) | Design choices `DX-n`, code map, open questions |
| [jemalloc_benchmark.md](jemalloc_benchmark.md) | Measured memory and throughput with and without jemalloc (TC-276) |
| [adr/](adr/) | [ADR-0011](adr/0011-jemalloc-keyspace-allocator.md) |
| [diagrams/](diagrams/) | PlantUML |
