# Connection: Traceability

One place for the links between use cases, requirements, scenarios and tests. Update a row in the same change that
changes any of them. The use cases have no acceptance criteria, so rows are per use case.

## Use cases

Coverage marks come from [test_plan.md](test_plan.md) and were matched by test names, not by reading every assertion.

| Use case | FR | NFR | Scenarios | Unit | Integration | Stress / Differential |
|----------|----|-----|-----------|------|-------------|-----------------------|
| [UC-005](use_cases/uc_005_ping.md) Ping | FR-007, FR-008, FR-014 | NFR-001, NFR-005 | TC-160 to 167 | Partial: `PING` and idle limit; no `ECHO`, no `PING` arity (TC-162, 164 Gap) | `ping_pong` TC-1001 to TC-1004 | Differential: `PING`, `ECHO`; stress phase 2 |
| [UC-006](use_cases/uc_006_connect.md) Connect over TCP | FR-008, FR-009, FR-010, FR-014 | NFR-001, NFR-002, NFR-004, NFR-005 | TC-168 to 192 | Mostly: listener, guard, session, pipeline, parser, request; query-buffer and output-drop partial | None; TC-172, 192 Gap (A-7) | Stress phases 1, 2 and 4; differential (error wording) |

## Non-functional requirements and how each is checked

The requirement text is in the [SRS](../../analysis/software_requirement_specification.md); only the means of
verification is recorded here.

| ID | Quality | Verification |
|----|---------|--------------|
| NFR-001 | Performance (pipelining) | redis-benchmark on `bench/` branches (FR-019); no scenario here |
| NFR-002 | Security (limits) | Unit tests of the guard and session (TC-171, 184, 187 to 190) |
| NFR-004 | Compatibility (RESP2, inline) | Parser and request unit tests; differential test of error wording |
| NFR-005 | Availability | Stress phase 1 (garbage input) and the listener fault test (TC-174, 186) |
