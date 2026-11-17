# Data Types: Traceability

One place for the links between use cases, requirements, scenarios and tests. These use cases have no acceptance
criteria sections, so rows are per use case. Update a row in the same change that changes any of them. Feature-level
rows roll up into the project [traceability matrix](../../quality/traceability_matrix.md).

## Use cases

Coverage marks come from [test_plan.md](test_plan.md) and were matched by test names, not by reading every assertion.

| Use case | FR                  | NFR              | Scenarios      | Unit                                                  | Integration                         | Stress / Differential                                  |
|----------|---------------------|------------------|----------------|-------------------------------------------------------|-------------------------------------|--------------------------------------------------------|
| UC-002   | FR-001, FR-006, FR-010, FR-012 | NFR-001, NFR-004 | TC-028 to 033 | Mostly: `string`, `set_ttl`, `aof`; `TC-029` has none | `set` (`TC-007` to `TC-014`)       | Differential sends `SET` with options; no stress       |
| UC-003   | FR-001, FR-006, FR-010 | NFR-001, NFR-004 | TC-034 to 036 | Partial: `string`; expired read only through `set_ttl` | `get` (`TC-015` to `TC-021`)        | Differential; stress reads with `GET`                  |
| UC-007   | FR-001, FR-010, FR-012 | NFR-001, NFR-004 | TC-037 to 045 | Partial: `string`; deadline and several type cases are gaps | None                              | Differential; stress `INCR` and `APPEND`               |
| UC-008   | FR-002, FR-010, FR-012 | NFR-001, NFR-004 | TC-046 to 051, 062 | Mostly: `list`; `LPUSH` order, bad pop count, arity are gaps | None                    | Differential; stress `LPUSH`, `RPUSH`, `LRANGE`        |
| UC-009   | FR-003, FR-010, FR-012 | NFR-001, NFR-004 | TC-052 to 055, 062 | Partial: `hash`; `HGETALL`, type and arity checks thin | None                              | Differential only; no stress (TC-064 is a gap)       |
| UC-010   | FR-004, FR-010, FR-012 | NFR-001, NFR-004 | TC-056 to 062 | Mostly: `set`, `aof` (`SPOP`); missing-key `SPOP` and bad count are gaps | None            | Differential (not `SPOP`); stress `SADD`, `SREM`, `SMEMBERS` |

Cross-cutting scenarios: TC-063 (differential run) applies to every row. The known limit LI-01 is not tied to a
use case; it pins today's behaviour.

The differential test sends no `SPOP` and no time-dependent command (master test plan section 5.2). The stress model
has no hashes and no pops from lists or sets.

## Non-functional requirements and how each is checked

The requirement text is in the [SRS](../../analysis/software_requirement_specification.md); only the means of
verification is recorded here.

| ID      | Quality       | Verification                                                              |
|---------|---------------|---------------------------------------------------------------------------|
| NFR-001 | Performance   | Benchmark with `redis-benchmark` (master test plan section 12); no scenario here |
| NFR-004 | Compatibility | Differential test against real Redis; error texts in the unit tests       |
