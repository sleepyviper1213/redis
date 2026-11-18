# ADR-0006: Own snapshot format (CBOR records)

- **Status:** Accepted (backfilled)
- **Date:** 2026-10-28 (`c7b0175`)
- **Requirements:** FR-011, NFR-003
- **Source:** SDD D6, §8.1

## Context

Snapshots must be crash-safe, versioned and binary-safe. The project is
Redis-*compatible* on the wire, not on disk.

## Decision

Write a own format: records encoded as CBOR through reflect-cpp, with a magic
number and format version checked on load. Files are replaced atomically:
temporary file, `fsync`, `rename`, `fsync` of the directory.

## Alternatives considered

- **Redis RDB:** large, version-bound, expensive to maintain.

## Consequences

- Small to maintain; `redis-check-rdb` and RDB tooling do not work on our files.
- A checksum is planned (SDD risk R-001). Absolute deadlines are stored, so reloading after downtime expires keys correctly.
