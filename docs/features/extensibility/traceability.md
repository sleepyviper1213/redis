# Extensibility: Traceability

One place for the links between use cases, requirements, scenarios and tests. Update a row in the same change that
changes any of them. The use case has no acceptance criteria, so rows are per use case.

## Use cases

Coverage marks come from [test_plan.md](test_plan.md) and were matched by test names, not by reading every assertion.

| Use case | FR | NFR | Scenarios | Unit | Integration | Stress / Differential |
|----------|----|-----|-----------|------|-------------|-----------------------|
| [UC-019](use_cases/uc_019_extend_features.md) Extend system features | BR-009, FR-019; FR-020 to FR-025 (Planned) | NFR-006, NFR-008, NFR-009 | TC-253 to 260; none for FR-020 to FR-025 | Mechanism: registration, dispatch, case and reply order tested; arity, timing and logging of a new command are Partial | None | Whole suite on CI; FR-019 benchmark on `bench/` branches |

## Non-functional requirements and how each is checked

The requirement text is in the [SRS](../../analysis/software_requirement_specification.md); only the means of
verification is recorded here.

| ID | Quality | Verification |
|----|---------|--------------|
| NFR-006 | Maintainability | Static analysis in CI; no scenario here |
| NFR-008 | Extensibility | TC-253 to 260: a command added through a table, with no change to the built-in ones; TC-309 to 319: `retry` policies (backoff, jitter) and `fail_after` for new background work |
| NFR-009 | Reusability | Not checked by a test |
