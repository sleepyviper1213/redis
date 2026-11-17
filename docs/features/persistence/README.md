# Feature: Persistence

| Item         | Value |
|--------------|-------|
| Feature ID   | FEAT-PERSISTENCE |
| Status       | Implemented; documented retroactively |
| Requirements | FR-011, FR-012 (defined in the [SRS](../../analysis/software_requirement_specification.md); not repeated here) |
| Issue / epic | TODO |

Snapshots and the append-only file.

## Use cases

| Use case | Actor | Commands / trigger | Requirements |
|----------|-------|--------------------|--------------|
| [UC-001 Save snapshot](use_cases/uc_001_save_snapshot.md) | Client | `SAVE`, `BGSAVE`, `LASTSAVE` | FR-011 |
| [UC-013 Rewrite the append-only file](use_cases/uc_013_rewrite_aof.md) | Client | `BGREWRITEAOF` | FR-012 |

## Diagrams

* Atomic replacement of a snapshot file, and what a reader sees at each step: [state_snapshot_file_replacement.puml](diagrams/state_snapshot_file_replacement.puml)
* `BGSAVE` and `BGREWRITEAOF`: forked child, housekeeping, new incremental file: [sequence_background_save_and_rewrite.puml](diagrams/sequence_background_save_and_rewrite.puml)

## Documents in this folder

| Document | Contents |
|----------|----------|
| [use_cases/](use_cases/) | The use cases of this feature |
| [test_plan.md](test_plan.md) | Test scenarios TC-nn, levels, coverage marks |
| [traceability.md](traceability.md) | Use case, requirement, scenario and test links, and how each NFR is verified |
| [design.md](design.md) | Design choices `DX-n`, code map, open questions |
| [adr/](adr/) | Decisions: ADR-0006, 0007 |
| [diagrams/](diagrams/) | PlantUML |
