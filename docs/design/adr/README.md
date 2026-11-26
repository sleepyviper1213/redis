# Architecture Decision Records

One file per significant decision, numbered in the order it was recorded
(not the order it was made), numbers shared across the whole project.

Where an ADR lives: a decision that belongs to one feature is in `features/<feature>/adr/`; a decision that
spans features or the build (errors as values, the toolchain) stays here. This page indexes all of them. Records are immutable once **Accepted**: to
change a decision, add a new ADR and mark the old one `Superseded by ADR-NNNN`.

Format: [adr_template.md](adr_template.md). Origin of the first ten: backfilled on
17 Nov 2026 from [SDD §15](../software_design_document.md#15-design-rationale)
and the git history; dates are those of the commit that first implemented the decision.

| ADR | Title | Status |
|-----|-------|--------|
| [0001](../../features/connection/adr/0001-single-threaded-command-executor.md) | One thread executes commands, no locks | Accepted |
| [0002](../../features/connection/adr/0002-stdexec-senders-over-callbacks.md) | stdexec senders for concurrency and cancellation | Accepted |
| [0003](../../features/connection/adr/0003-coroutines-only-for-branching-loops.md) | Coroutines only for loops that branch per connection | Accepted |
| [0004](../../features/data_types/adr/0004-zero-copy-request-arguments.md) | Zero-copy request arguments | Accepted |
| [0005](../../features/key_expiry/adr/0005-two-expiry-indexes.md) | Two indexes for key expiry | Accepted |
| [0006](../../features/persistence/adr/0006-own-snapshot-format.md) | Own snapshot format (CBOR records) | Accepted |
| [0007](../../features/persistence/adr/0007-fork-for-background-save.md) | `fork` for background save and AOF rewrite | Accepted |
| [0008](../../features/server_lifecycle/adr/0008-refuse-to-start-on-unreadable-data.md) | Refuse to start on unreadable data | Accepted |
| [0009](0009-errors-as-values.md) | Errors as values, not exceptions | Accepted |
| [0010](0010-pinned-vcpkg-and-layered-presets.md) | Pinned vcpkg baseline and layered CMake presets | Accepted |
| [0011](../../features/observability/adr/0011-jemalloc-keyspace-allocator.md) | jemalloc as the keyspace allocator | Proposed |
| [0012](../../features/connection/adr/0012-session-decisions-apart-from-io.md) | A session's decisions live apart from its I/O | Accepted |
| [0013](0013-timers-over-std-chrono-clocks.md) | Elapsed and deadline timers share one interface over std::chrono clocks | Accepted |
