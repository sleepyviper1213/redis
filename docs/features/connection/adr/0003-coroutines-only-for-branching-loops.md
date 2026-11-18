# ADR-0003: Coroutines only for loops that branch per connection

- **Status:** Accepted (backfilled)
- **Date:** 2026-11-17 (`02dd817`; earlier erasure attempt `a0a1bc3`; `7432151`)
- **Requirements:** NFR-001, NFR-006
- **Source:** SDD D3

## Context

Session and accept loops decide what to do next based on each connection's
state. Pure sender chains for them instantiate many templates and, if erased,
cost an allocation per round.

## Decision

Use a coroutine for the loops that branch per connection (the session round
loop, the accept loop); keep everything else as senders. The accept loop takes
an `AcceptSource` interface instead of four `std::function` hooks (`7432151`).

## Alternatives considered

- **Coroutines for the whole session:** a frame per call, and errors become exceptions.
- **Type-erasing each round (`a0a1bc3`):** replaced by the coroutine loop in `02dd817`; `erased_per_start` was then removed (`2fca0c8`).

## Consequences

- One frame per loop instead of per round; less template instantiation.
- Error handling inside the coroutine must still convert to values at its boundary ([ADR-0009](../../../design/adr/0009-errors-as-values.md)).
