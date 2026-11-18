# ADR-0001: One thread executes commands, no locks

- **Status:** Accepted (backfilled)
- **Date:** 2026-10-28 (`fb3b150`, refined `f13088f`, `77bafeb`)
- **Requirements:** FR-009, NFR-001
- **Source:** SDD D1, §14.2

## Context

Several clients are served at once, and every command must be atomic. The
keyspace is a plain hash map; making it thread-safe would put a cost on every
command.

## Decision

Network I/O may use several threads (`--io-threads`), but exactly one thread
executes command batches and housekeeping. The keyspace has no locks. Debug
builds assert the owner thread (`77bafeb`). With `--io-threads 1` the main
thread does both, as Redis does.

## Alternatives considered

- **Shards with a lock each:** lock cost on every command; multi-key commands
  would need ordered locking.

## Consequences

- Every command is atomic without further work; expiry and reaping never race a command.
- A slow command blocks every client; the watchdog (FR-016) reports it.
- Execution does not scale across cores; I/O does.
