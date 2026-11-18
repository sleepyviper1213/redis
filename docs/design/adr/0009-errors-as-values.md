# ADR-0009: Errors as values, not exceptions

- **Status:** Accepted (backfilled)
- **Date:** 2026-08-28 (`b547fb8`, `std::expected` helpers); extended 2026-11-10 (`7171e76`)
- **Requirements:** NFR-006, FR-010
- **Source:** SDD D9, §14.5

## Context

Parsing, persistence and I/O fail routinely. The failures must reach the client as replies and the log as one consistent sentence.

## Decision

Recoverable errors are `std::expected<T, Failure<Kind>>`, with a kind enum per
module (`ConnectionKind`, `PersistKind`, `ServerKind`) and one `display`
function producing the wording. Exceptions are for what a library throws and
for crashing. Commands return `resp::Value`; `REDIS_TRY` and `FAILED` remove the boilerplate.

## Alternatives considered

- **Exceptions:** hidden control flow and a cost on the hot path.

## Consequences

- Visible control flow; a log line and an `ERR` reply agree.
- Every call site must propagate explicitly; library exceptions (e.g. Asio failing to bind) still need a boundary.
