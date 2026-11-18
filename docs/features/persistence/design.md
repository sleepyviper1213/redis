# Persistence: Design Choices

Choices specific to this feature. System-wide design stays in the
[SDD](../../design/software_design_document.md); a decision that was a real trade-off has an
[ADR](../../design/adr/). Rows are added when the feature changes.

| #        | Choice | Why | Where recorded |
|----------|--------|-----|----------------|
| DX-24 | Own snapshot file: magic `RCPPSNAP`, version, count, then size-framed CBOR records (reflect-cpp); keys and values are byte strings | Small to maintain, binary-safe, no RDB version lock; a corrupt size cannot make loading over-allocate | [ADR-0006](adr/0006-own-snapshot-format.md), SDD §8.1, D6 |
| DX-25 | Files are replaced atomically: temp file in the same directory, `fsync`, `rename`, `fsync` of the directory | A reader sees the old file or the new one, never a partial one | SDD §13.2, NFR-003 |
| DX-26 | `BGSAVE` and the AOF rewrite `fork` a child that writes from a copy-on-write view; the child ends with `_exit`; housekeeping reaps it without blocking | Consistent snapshot, no locking, no up-front copy; POSIX only | [ADR-0007](adr/0007-fork-for-background-save.md), SDD §13.3, D10 |
| DX-27 | The AOF is a directory: manifest, a base snapshot, incremental RESP files, a lock file held for the server's life | The base plus the ordered incrementals is the state | SDD §8.2 |
| DX-28 | The rewrite starts a new incremental file before the fork; the manifest lists only the new base after the child succeeds | No buffering; a crash at any step leaves a replayable log | [ADR-0007](adr/0007-fork-for-background-save.md), SDD §13.4, D7 |
| DX-29 | Commands not replayable as received are logged as equivalents (`SET ... EX` as `PXAT`, `EXPIRE` as `PEXPIREAT`, `SPOP` as `SREM`); snapshots hold absolute deadlines | A replay cannot extend lifetimes or pop other members | SDD §8.2, FR-012 |
| DX-30 | Fsync policy `always` (before the reply), `everysec` (a thread, default) or `no`; a failed `always` sync exits the server | Operator picks the durability/latency trade (exit: from code) | SDD §13.6; `src/server/persistence/append_only_file.cpp` |
| DX-31 | A load failure of a snapshot or AOF stops startup | Serving empty and later saving would destroy the only copy | [ADR-0008](../server_lifecycle/adr/0008-refuse-to-start-on-unreadable-data.md), SDD D8, NFR-003 |
| DX-32 | Persistence returns `std::expected<T, Failure<PersistKind>>`; a failed `SAVE` is `ERR` plus the reason and the previous file stays | One wording for log and reply | [ADR-0009](../../design/adr/0009-errors-as-values.md), SDD §14.5 |
| DX-33 | Data files are held by descriptor and not re-opened by path | Avoids a path naming a different file between two uses (from code) | `src/core/os/durable_file.hpp` |
| DX-77 | Data-loss bound: `always` loses no acknowledged write; `everysec` (default) loses at most the writes of the last second | Stated per policy so an operator can choose; covered by TC-261 | SRS NFR-003, SDD §13.6, TC-261 |

## Code map

* `src/server/persistence/` : `snapshot_manager.hpp`, `snapshot_manager.cpp`, `append_only_file.hpp`, `append_only_file.cpp`, `aof_config.hpp`, `persist_error.hpp`, `persist_error.cpp`, `detail/snapshot.hpp`, `detail/snapshot.cpp`.
* `src/core/os/` : `save_file.hpp`, `save_file.cpp` (atomic replace), `durable_file.hpp`, `child_process.hpp`, `posix.cpp`, `windows.cpp` (no `fork`).
* `src/server/commands/server.cpp` : `SAVE`, `BGSAVE`, `LASTSAVE`, `BGREWRITEAOF`.

## Open questions

* Resolved: after a FailFast end the server attempts a final snapshot and exits non-zero if it fails (DX-76 in [server_lifecycle](../server_lifecycle/design.md)).
* `BGSAVE` and `BGREWRITEAOF` are unavailable on Windows (SDD §17).
* Snapshots are not RDB files, so RDB tools cannot read them (SDD §17).
* Deferred: the rewrite layer for AOF and replication stays in TODO.md and is not in scope until one of them is added. The equivalents that SDD §8.2 describes stay as they are.
* Deferred: a snapshot checksum (SRS R-001); no scenario until it exists.
* Reflect-cpp with CBOR is the snapshot encoding (DX-24, ADR-0006); the SRS now says the same.
