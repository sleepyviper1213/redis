# Requirements Traceability Matrix

**Document ID:** RTM-001 · **Revision:** 0.1 (seeded) · **Date:** 17 Nov 2026
**Structure:** ISO/IEC/IEEE 29148 §5.2.8

Chain: requirement (SRS) → design (SDD) → code → test → issue/PR.
Design column seeded from [SDD §16](../design/software_design_document.md#16-requirements-traceability);
**Tests and Issues are still to fill in.** The "Issues" column is generated with
`gh issue list --search "<id>"` once the Requirement field is in use.

| Requirement                | Design (SDD §)   | Code (module)       | Tests (TEST-001 ref) | Issues |
|----------------------------|------------------|---------------------|----------------------|--------|
| FR-001–005 Data commands   | 5, 6.3           | `server/commands`   | TODO | TODO |
| FR-006 Key expiry          | 6.2, 13.1        | `server/storage`    | [features/key_expiry/traceability.md](../features/key_expiry/traceability.md) | TODO |
| FR-007 Server commands     | 5.1              | `server/commands`   | TODO | TODO |
| FR-008 TCP and protocol    | 5.1, 11.1        | `core/resp`, `server/net` | [features/connection/traceability.md](../features/connection/traceability.md) (fuzz: TC-300, 307, 308) | #51 |
| FR-009 Concurrent clients  | 14.1, 14.2       | `server/net`        | TODO | TODO |
| FR-010 Error replies       | 10.1, 14.5       | `server/commands`   | TODO | TODO |
| FR-011 Snapshots           | 8.1, 13.2, 13.3  | `server/persistence`| TODO | TODO |
| FR-012 Append-only file    | 8.2, 13.4, 13.6  | `server/persistence`| TODO | TODO |
| FR-013 Configuration       | 8.3              | `server`            | TODO | TODO |
| FR-014 Client limits       | 14.4             | `server/net`        | TODO | TODO |
| FR-015 Logging             | 5.1              | `core/logging`      | TODO | TODO |
| FR-016 Diagnostics         | 14.1, 10.2       | `server/net`        | TODO | TODO |
| FR-017 Graceful shutdown   | 11.2, 14.3       | `server`            | TODO | TODO |
| FR-018 Startup safety      | 11.2, 14.5       | `server`            | TODO | TODO |
| FR-019 Benchmarking        | [5]              | `scripts`           | [benchmark_report.md](../evaluation/benchmark_report.md) | TODO |
| NFR-001 Performance        | 6.4, 11.1        | whole               | benchmark | TODO |
| NFR-003 Reliability        | 13.2, 13.4       | `server/persistence`| TODO | TODO |
| NFR-005 Availability       | 13.3, 13.4       | `server/persistence`| TODO | TODO |
| NFR-006–009 Quality        | 7, 9             | whole               | CI, static analysis | TODO |
| NFR-002, 004, 010          | TODO             | TODO                | TODO | TODO |
