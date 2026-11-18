# Keyspace: Traceability

One place for the links between use cases, requirements, scenarios and tests. These use cases have no acceptance
criteria sections, so rows are per use case. Update a row in the same change that changes any of them. Feature-level
rows roll up into the project [traceability matrix](../../quality/traceability_matrix.md).

## Use cases

Coverage marks come from [test_plan.md](test_plan.md) and were matched by test names, not by reading every assertion.

| Use case | FR                     | NFR              | Scenarios                | Unit                                                       | Integration                          | Stress / Differential                                   |
|----------|------------------------|------------------|--------------------------|------------------------------------------------------------|--------------------------------------|---------------------------------------------------------|
| UC-004   | FR-005, FR-010, FR-012 | NFR-001, NFR-004 | TC-065 to 075           | Mostly: `keyspace`, `database`; `FLUSHDB` options and the log are gaps or partial | `del` (`TC-022` to `TC-027`) | Stress `DEL` against a model; differential `DEL`, `FLUSHDB` |
| UC-012   | FR-005, FR-006, FR-010 | NFR-001, NFR-004 | TC-076 to 084           | Partial: `keyspace`, `glob`; `TYPE` of hash and set, empty `KEYS`, arity are gaps | None                          | Differential `KEYS *`, `EXISTS`, `TYPE`, `DBSIZE`       |

The `TTL`, `PTTL` and `PERSIST` commands of FR-005 are scenario TC-085 here and TC-104a, 104b in
[key_expiry](../key_expiry/test_plan.md). The limits by design LI-02 and LI-03 are not tied to a use case; they pin
today's behaviour.

## Non-functional requirements and how each is checked

The requirement text is in the [SRS](../../analysis/software_requirement_specification.md); only the means of
verification is recorded here.

| ID      | Quality       | Verification                                                              |
|---------|---------------|---------------------------------------------------------------------------|
| NFR-001 | Performance   | Benchmark with `redis-benchmark` (master test plan section 12); no scenario here. `KEYS` is linear by design (LI-02) |
| NFR-004 | Compatibility | Differential test against real Redis; error texts in the unit tests       |
