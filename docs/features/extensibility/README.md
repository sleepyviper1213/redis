# Feature: Extensibility

| Item         | Value |
|--------------|-------|
| Feature ID   | FEAT-EXTENSIBILITY |
| Status       | Implemented; documented retroactively |
| Requirements | BR-009, FR-020 to FR-025 (defined in the [SRS](../../analysis/software_requirement_specification.md); not repeated here) |
| Issue / epic | TODO |

Adding commands and the planned features.

## Use cases

| Use case | Actor | Commands / trigger | Requirements |
|----------|-------|--------------------|--------------|
| [UC-019 Extend system features](use_cases/uc_019_extend_features.md) | Operator | New commands and planned features | BR-009, FR-020–FR-025 |

## Diagrams

* Command tables merged into the handler, then dispatch with the arity check: [activity_command_registration_and_dispatch.puml](diagrams/activity_command_registration_and_dispatch.puml)

## Documents in this folder

| Document | Content |
|----------|---------|
| [test_plan.md](test_plan.md) | Test scenarios `TC-nn` (scope and mechanism only; the planned features are not testable yet) |
| [traceability.md](traceability.md) | Use cases to requirements, scenarios and tests |
| [design.md](design.md) | Design choices `DX-n`, code map, open questions |
| [diagrams/](diagrams/) | PlantUML |
