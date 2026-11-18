# Key Expiry: Traceability

One place for the links between acceptance criteria, use cases, requirements, tests and diagrams. Update a row in
the same change that changes any of them. Feature-level rows roll up into the project
[traceability matrix](../../quality/traceability_matrix.md).

## Acceptance criteria

Coverage marks come from [test_plan.md](test_plan.md) and were matched by test names, not by reading every assertion.

| Criteria  | Use case | FR                      | NFR                     | Scenarios               | Unit    | Integration | Differential | Diagram                    |
|-----------|----------|-------------------------|-------------------------|-------------------------|---------|-------------|--------------|----------------------------|
| AC-01 | UC-011 | FR-006.1, 006.6 | NFR-010.4 | TC-086 to 092, 100, 101 | Yes | n/a | None | sequence_key_expiry |
| AC-02 | UC-011 | FR-006.1, 006.5, 006.6 | NFR-010.4 | TC-088, 089, 098, 099 | Yes | n/a | None | state_key_expiry_lifecycle |
| AC-03 | UC-002 | FR-001, FR-006.2 | NFR-010.4 | TC-093 to 097, 102, 263 | Yes | None | None | use_case (global) |
| AC-04 | UC-011 | FR-005, FR-006.3 | NFR-010.4 | TC-104, 104a | Yes | n/a | None | sequence_key_expiry |
| AC-05 | UC-011 | FR-005, FR-006.4 | NFR-010.4 | TC-104b | Yes | n/a | None | state_key_expiry_lifecycle |
| AC-06 | UC-011 | FR-006.7 | NFR-010.4 | TC-094, 095, 104c to 104e | Yes | n/a | None | state_key_expiry_lifecycle |
| AC-07 | UC-020 | FR-006.8 | NFR-010.1 | TC-105, 106, 106a to 106c | Yes | Partial (1/2) | n/a | sequence_key_expiry |
| AC-08 | UC-020 | FR-006.9 | NFR-010.2, 010.3, 010.6 | TC-107 to 110a | Yes | Yes | n/a | activity_expiry_sweep |
| AC-09 | UC-021 | FR-006.10 | NFR-010.5, NFR-003 | TC-111 to 119 | Yes | Partial (2/3) | n/a | sequence_key_expiry |
| AC-10 | UC-022 | FR-006.11 | NFR-010.7 | TC-120 to 122 | Yes | Partial (1/3) | n/a | sequence_key_expiry |

The bug #40, the unimplemented #41 and #42, and the limit by design LI-05 are not tied to a criterion; see
[test_plan.md](test_plan.md). TC-103 (arity,
FR-010) and TC-123 (deadline arithmetic, NFR-010.1) check a requirement directly, not a criterion.

The differential test sends nothing that depends on time (master test plan §5.2). Scenarios marked Differential can
join it only in a time-independent form. That is a change to `scripts/diff_test.py`, not part of this feature yet.

## Non-functional requirements and how each is checked

The requirement text is NFR-010 in the [SRS](../../analysis/software_requirement_specification.md); only the means of
verification is recorded here.

| ID        | Quality       | Verification                                    |
|-----------|---------------|-------------------------------------------------|
| NFR-010.1 | Correctness   | Unit tests on the keyspace and `SET` options    |
| NFR-010.2 | Timeliness    | Integration test with a short `PEXPIRE`         |
| NFR-010.3 | Bounded cost  | Review of the keyspace; benchmark with `SET EX` |
| NFR-010.4 | Compatibility | Differential test (time-independent cases)      |
| NFR-010.5 | Durability    | Unit tests of the snapshot and the log          |
| NFR-010.6 | Safety        | ThreadSanitizer in CI                           |
| NFR-010.7 | Observability | Integration test of `INFO`                      |

## Design choices to requirements

| Choice   | Requirements |
|----------|--------------|
| DX-17 | FR-006.3, FR-006.4, FR-006.9 |
| DX-18 | FR-006.8, FR-006.9, NFR-010.1 |
| DX-19 | FR-006.9, NFR-010.2, NFR-010.3 |
| DX-20 | NFR-010.6 |
| DX-21 | FR-006.10, NFR-010.5 |
| DX-22 | FR-006.10, NFR-010.5 |
| DX-23 | FR-006.5 |

Choices are defined in [design.md](design.md).
