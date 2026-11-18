# ADR-0007: `fork` for background save and AOF rewrite

- **Status:** Accepted (backfilled)
- **Date:** 2026-10-28 (`c7b0175`)
- **Requirements:** FR-011, FR-012, NFR-005
- **Source:** SDD D10, D7, §13.3, §13.4

## Context

`BGSAVE` and the append-only rewrite must not block clients, and the single
executor ([ADR-0001](../../connection/adr/0001-single-threaded-command-executor.md)) cannot lock the keyspace while copying it.

## Decision

Fork a child process that writes the file from a copy-on-write view. For the
AOF rewrite, start a new incremental file *before* forking, so nothing is buffered meanwhile (SDD D7).

## Alternatives considered

- **A thread copying the keyspace:** needs locks or an up-front copy.
- **Buffering writes during the rewrite:** memory grows during the rewrite.

## Consequences

- No locking, no up-front copy; a crash at any step leaves a replayable log.
- POSIX only: Windows builds have no `BGSAVE`. The child must keep the crash log open (`74afb7f`) and tests allow up to 10 s for it (`1bce6dc`).
