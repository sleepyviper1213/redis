# ADR-0002: stdexec senders for concurrency and cancellation

- **Status:** Accepted (backfilled)
- **Date:** 2026-10-28 (`fb3b150`)
- **Requirements:** FR-009, FR-017, NFR-006
- **Source:** SDD D2, §14.3

## Context

The server needs structured shutdown (cancel the accept loop, drain sessions,
then stop the loop), per-request timeouts, and supervision of tasks with
different failure policies.

## Decision

Build on the C++26 sender/receiver model (stdexec) with Asio's `use_sender`
completions. Tasks are raced and cancelled through stop tokens; sessions are
tracked by a counting scope and drained.

## Alternatives considered

- **Callbacks:** cancellation and ownership are hand-written per call site.
- **Coroutines everywhere:** see [ADR-0003](0003-coroutines-only-for-branching-loops.md).

## Consequences

- No frame is allocated per request; the shutdown race is a few lines.
- The dependency is young: the vcpkg baseline had to be bumped when
  `stdexec::task` became a sender (`5b7ee9e`), and `InlineTask` was rebuilt on
  `stdexec::task` (`26a4c97`). Pinning is covered by [ADR-0010](../../../design/adr/0010-pinned-vcpkg-and-layered-presets.md).
- A stop requested before a `use_sender` wait starts is lost; timers use
  `unless_stop_requested` (see `TODO.md`, request timeouts).
