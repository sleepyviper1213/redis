# Key Expiry: Design Choices

Choices specific to this feature. System-wide design stays in the
[SDD](../../design/software_design_document.md); a decision that was a real trade-off has an
[ADR](../../design/adr/). Rows are added when the feature changes.

| #       | Choice | Why | Where recorded |
|---------|--------|-----|----------------|
| DX-17 | Two indexes: a map of key to deadline, and an ordered set of (deadline, key) | O(1) `TTL`, `PTTL`, `PERSIST`; the sweep takes the oldest due keys in O(log n) each | [ADR-0005](adr/0005-two-expiry-indexes.md), SDD §6.2 |
| DX-18 | Lazy removal on access, plus an active sweep | A key nobody reads still leaves memory; no command ever sees a due key | SDD §13.1, FR-006.8, FR-006.9 |
| DX-19 | Sweep every 100 ms, at most 1000 keys, oldest first | A tick stays short so clients are not held up; a key may stay in memory a little past its deadline | SDD §13.1, NFR-010.2, NFR-010.3 |
| DX-20 | Sweep and commands on the one keyspace thread | They never overlap, so no locks and no race between expiry and a command | [ADR-0001](../connection/adr/0001-single-threaded-command-executor.md), NFR-010.6 |
| DX-21 | Deadlines are absolute wall-clock times | `EXPIREAT`, `PXAT` and snapshots name calendar times; the cost is clock sensitivity (see known limits in the [README](README.md)) | FR-006.10 |
| DX-22 | Snapshots and the append-only file store absolute deadlines; expired keys are logged as `DEL`; replay runs with expiry off | A restart or a replay can never extend a lifetime | [ADR-0006](../persistence/adr/0006-own-snapshot-format.md), FR-006.10, NFR-010.5 |
| DX-23 | A deadline already past deletes the key at once | Matches Redis; no entry is created that is due from the start | FR-006.5 |

## Code map

Per the [master test plan §12](../../test/test_plan_and_report.md): `server/commands/set_ttl`,
`core/chrono/deadline_timer`, `common/store/database` (`basic_database.hpp`), and the persistence code in
`server/persistence` (`set_expiry_enabled` switches expiry off during a replay).
Check this list against the source when the design changes.

## Open questions

* Support `NX`/`XX`/`GT`/`LT` and `EXPIRETIME`? Not required by the SRS today; would add requirements and change #41 and #42.
* Make `DEL` of a due key reply `:0`, as Redis does? Pinned today by #40.
