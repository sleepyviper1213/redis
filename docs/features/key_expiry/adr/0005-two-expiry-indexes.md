# ADR-0005: Two indexes for key expiry

- **Status:** Accepted (backfilled)
- **Date:** 2026-10-28 (`58c96ca`, typed keyspace with lazy and active expiry)
- **Requirements:** FR-006, NFR-010
- **Source:** SDD D5, §6.2, §13.1; `docs/features/key_expiry/`

## Context

Keys must expire lazily (on access) and actively (a sweep), and `TTL`/`PTTL`/`PERSIST` must be cheap.

## Decision

Keep `expires_` (hash map, key to deadline) and `by_deadline_` (ordered set of
deadline, key). The sweep removes the oldest due keys from the ordered set,
bounded per tick.

## Alternatives considered

- **One map with random sampling (Redis' approach):** `TTL` is O(1), but sweep cost and coverage are probabilistic.

## Consequences

- O(1) `TTL`, O(log n) per removed key, ordered and bounded sweep.
- Two structures to keep consistent on every set/persist/delete; extra memory per expiring key. An owner check in `set_expiry` (`b228ab8`) guards this.
