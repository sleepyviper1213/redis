# ADR-0004: Zero-copy request arguments

- **Status:** Accepted (backfilled)
- **Date:** 2026-10-29 (`b11bc50`, fast path for arrays of bulk strings)
- **Requirements:** NFR-001
- **Source:** SDD D4, §6.4

## Context

Copying each argument allocates on every request, which dominates cost for
small commands and large values.

## Decision

Arguments are a `span` of `string_view` into the session's read buffer, valid
until the batch has finished. Replies are encoded directly into the output
buffer. A command copies an argument only when it stores it.

## Alternatives considered

- **Copy each argument:** simple and safe, but more allocations.

## Consequences

- The main reason large-value `GET` beats Redis in the benchmarks (SDD [5]).
- Views dangle if kept past the batch; commands must copy anything they store, and the read buffer must not move while a batch executes.
