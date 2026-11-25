# Server Lifecycle: Traceability

One place for the links between use cases, requirements, scenarios and tests. Update a row in the same change that
changes any of them. These use cases have no acceptance-criteria sections, so rows are per use case.

Coverage marks come from [test_plan.md](test_plan.md) and were matched by test names, not by reading every assertion.

| Use case | FR | NFR | Scenarios | Unit | Integration | Stress/Differential |
|----------|----|-----|-----------|------|-------------|---------------------|
| UC-016 Configure and start: options (steps 1, 2; A1) | FR-013 | NFR-006 | TC-227 to 232 | None | None (A-2) | None |
| UC-016 Configure and start: startup and data (steps 3 to 7; A2 to A6) | FR-018, FR-011, FR-012, FR-015 | NFR-003, NFR-007 | TC-233 to 240, 295 | Partial (loaders refuse bad files, torn log, directory lock) | Partial (the scripts start the binary; no scenario inspects exit status or log lines) | Stress script starts the server |
| UC-017 Stop the server (steps 1 to 8; A1 to A3) | FR-017, FR-011, FR-012 | NFR-003, NFR-005 | TC-241 to 252, 296 | Partial (listener stop, logger flush) | Partial (`stop_server` checks exit 0 after `SIGTERM`; final save untested, A-6) | The stress and benchmark scripts stop the server with `SIGTERM` |

Differential testing does not apply to this feature.

## Non-functional requirements and how each is checked

The requirement text is in the [SRS](../../analysis/software_requirement_specification.md); only the means of
verification is recorded here. NFR-005, NFR-006 and NFR-007 are named by the use cases and belong to other plans; only
NFR-003 is traced through this feature.

| ID      | Quality     | Verification                                                                                           |
|---------|-------------|--------------------------------------------------------------------------------------------------------|
| NFR-003 | Reliability | Unit tests of the loaders refusing bad files (TC-236, 237, 238); integration test of the final save and its failure (TC-248, 249, both Gaps) |
