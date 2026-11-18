# Definition of Ready and Done

**Document ID:** DOD-001 · **Revision:** 1.0 · **Date:** 17 Nov 2026

## Definition of Ready (issue may move to *Ready*)

- [ ] Names a requirement ID, or a clear reason it has none.
- [ ] Acceptance criteria are written and testable.
- [ ] Size is set; anything XL is split first.
- [ ] Dependencies are linked (blocked-by issues).

## Definition of Done (issue may move to *Done*)

- [ ] Code merged to `main` through a PR that says `Closes #n`.
- [ ] CI green on every preset it runs on, sanitizers included.
- [ ] Tests added or updated; level and result recorded per [TEST-001](../test/test_plan_and_report.md).
- [ ] Docs updated where behaviour changed: SRS, SDD, user guide, use case.
- [ ] A new design decision has an ADR.
- [ ] Traceability row updated in [traceability_matrix.md](../quality/traceability_matrix.md).
- [ ] No new compiler or analyser warning.
- [ ] Performance-relevant change measured with `redis-benchmark` and noted in the PR.
