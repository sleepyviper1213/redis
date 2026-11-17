# Feature: Keyspace

| Item         | Value |
|--------------|-------|
| Feature ID   | FEAT-KEYSPACE |
| Status       | Implemented; documented retroactively |
| Requirements | FR-005 (defined in the [SRS](../../analysis/software_requirement_specification.md); not repeated here) |
| Issue / epic | TODO |

Deleting keys and inspecting the keyspace.

## Use cases

| Use case | Actor | Commands / trigger | Requirements |
|----------|-------|--------------------|--------------|
| [UC-004 Delete key(s)](use_cases/uc_004_delete_keys.md) | Client | `DEL`, `FLUSHDB` | FR-005 |
| [UC-012 Inspect the keyspace](use_cases/uc_012_inspect_keyspace.md) | Client | `EXISTS`, `KEYS`, `TYPE`, `DBSIZE` | FR-005 |

## Documents in this folder

| Document | Content |
|----------|---------|
| [test_plan.md](test_plan.md) | Test scenarios `TC-nn` and their coverage by existing tests |
| [traceability.md](traceability.md) | Use case to requirement, scenario and test |
| [design.md](design.md) | Design choices `DX-n`, code map, open questions |
| [diagrams/](diagrams/) | PlantUML |

## Diagrams

* Commands against the keyspace: [activity_keyspace_commands.puml](diagrams/activity_keyspace_commands.puml)
