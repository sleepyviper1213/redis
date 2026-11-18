# ADR-0008: Refuse to start on unreadable data

- **Status:** Accepted (backfilled)
- **Date:** 2026-10-28 (`d8cf3ad`, composition root with snapshot load)
- **Requirements:** FR-018, NFR-003
- **Source:** SDD D8, §11.2, §14.5

## Context

If the snapshot or AOF cannot be read, the server could start empty and carry on.

## Decision

A load failure is fatal: log the reason and exit. Startup does not continue with an empty keyspace.

## Alternatives considered

- **Start empty and log:** a later save, or the shutdown save, would overwrite the only copy.

## Consequences

- Data is never destroyed silently; an operator must move or repair the file.
- A corrupt file makes the service unavailable until someone acts.
- AOF manifest file names are validated before use (`70960f2`).
