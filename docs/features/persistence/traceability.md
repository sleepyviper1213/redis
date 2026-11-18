# Persistence: Traceability

One place for the links between use cases, requirements, scenarios and tests. Update a row in the same change that
changes any of them. These use cases have no acceptance-criteria sections, so rows are per use case.

Coverage marks come from [test_plan.md](test_plan.md) and were matched by test names, not by reading every assertion.

| Use case | FR | NFR | Scenarios | Unit | Integration | Stress/Differential |
|----------|----|-----|-----------|------|-------------|---------------------|
| UC-001 Save snapshot (`SAVE`, `LASTSAVE`, load) | FR-011, FR-010, FR-018 | NFR-003, NFR-001 | TC-124 to 134, 140, 141 | Mostly (arity and outcome logging missing) | Hidden, never run by default (A-5) | None |
| UC-001 A2, A3 (`BGSAVE`) | FR-011 | NFR-005, NFR-003 | TC-135 to 139 | Partial (`[fork]`, skipped on Windows) | Hidden `TC-006` only | None; TC-139 is a gap |
| UC-013 Rewrite the append-only file | FR-012, FR-010 | NFR-003, NFR-005, NFR-007 | TC-142 to 156 | Partial (success path only; failure and shutdown paths missing) | None (A-7) | None |
| Both (files that cannot be trusted) | FR-011, FR-012, FR-018 | NFR-003 | TC-157 to 159 | Yes | None | None |

Differential testing does not apply: the snapshot and log formats are this project's own.

## Non-functional requirements and how each is checked

The requirement text is in the [SRS](../../analysis/software_requirement_specification.md); only the means of
verification is recorded here. NFR-001, NFR-005 and NFR-007 are named by the use cases but belong to other plans; only
NFR-003 is traced through this feature.

| ID      | Quality     | Verification                                                                                           |
|---------|-------------|--------------------------------------------------------------------------------------------------------|
| NFR-003 | Reliability | Unit tests of `save_file` (atomic replace), `snapshot` (rejects bad files), `aof` (torn tail, manifest, lock); crash recovery end to end is a Gap (TC-150, TC-154, TC-155) |
