# Observability: Traceability

One place for the links between use cases, requirements, scenarios and tests. Update a row in the same change that
changes any of them. The use cases have no acceptance criteria, so rows are per use case.

## Use cases

Coverage marks come from [test_plan.md](test_plan.md) and were matched by test names, not by reading every assertion.

| Use case | FR | NFR | Scenarios | Unit | Integration | Stress / Differential |
|----------|----|-----|-----------|------|-------------|-----------------------|
| [UC-014](use_cases/uc_014_inspect_server.md) Inspect the server | FR-007, FR-014 | NFR-004, NFR-006 | TC-193 to 209, TC-272 to 277 | Partial: counters, memory, one-section and connection fields tested; selectors, persistence fields, `CONFIG GET` patterns and the unsupported form are Gap | None | None (differential does not send `INFO`/`CONFIG`) |
| [UC-015](use_cases/uc_015_change_log_level.md) Change the log level | FR-007, FR-015 | NFR-002, NFR-006 | TC-210 to 216 | Mostly: apply, refusals, invalid level; one level of seven tested | None; the warning line and the default-off start-up are Gap | None |
| [UC-018](use_cases/uc_018_watch_logs.md) Watch the logs | FR-015, FR-016 | NFR-003, NFR-006 | TC-217 to 226 | Partial: watchdog and async flush tested; log format, wording, crash report and error logging are Gap | None (A-3) | None |

## Non-functional requirements and how each is checked

The requirement text is in the [SRS](../../analysis/software_requirement_specification.md); only the means of
verification is recorded here.

| ID | Quality | Verification |
|----|---------|--------------|
| NFR-002 | Security (`CONFIG SET` off by default) | Unit test of the refusal (TC-213); integration test of the start-up default (TC-214, Gap) |
| NFR-003 | Reliability (crash report, flushed warnings) | Integration tests that crash a child on purpose (TC-219, 224; Gap) |
| NFR-004 | Compatibility (`INFO` field names as in Redis) | Unit tests of field names; no differential test of `INFO` yet |
| NFR-001 | Performance (keyspace allocator) | Benchmark with and without jemalloc (TC-276, Gap) |
| NFR-006 | Maintainability (observability) | The scenarios of this feature |
