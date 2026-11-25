# ADR-0011: jemalloc as the keyspace allocator

- **Status:** Proposed
- **Date:** 2026-11-19
- **Requirements:** FR-007 (`INFO memory`), NFR performance
- **Issue / PR:** #50

## Context

The keyspace is parameterised on a standard Allocator (`BasicDatabase<Allocator>`, `BasicValueTypes<Allocator>`);
`DefaultAllocator` is `CountingAllocator`, which forwards to `std::allocator` and adds up the bytes for
`INFO used_memory` (DX-48). A server that creates and deletes many keys of mixed sizes fragments the C library's heap:
resident memory stays above `used_memory`. Redis itself ships jemalloc for this reason.

Constraints: jemalloc builds on Linux and macOS but not on MinGW or MSVC here; there is no Linux machine, so Linux
work runs through GitHub Actions; the sanitizer presets must keep seeing every allocation.

## Decision

Add `JemallocAllocator<T>`, a stateless standard Allocator over jemalloc's extended API (`mallocx`, `nallocx`, `sdallocx`, the unprefixed names `jemalloc.h` provides), and make it
`DefaultAllocator` when the build option `REDIS_USE_JEMALLOC` is on and the platform has jemalloc. Otherwise
`DefaultAllocator` stays `CountingAllocator`. The allocator keeps the process-wide byte counter, and counts the usable
size (`nallocx`) rather than the requested size, so `used_memory` is what the allocator really holds.

The option is off until the benchmark (TC-276) shows a gain.

## Measurement

First run (Benchmark workflow #27, 2026-11-20; details in [jemalloc_benchmark.md](../jemalloc_benchmark.md)): after
filling, deleting most keys and refilling with a different value size, RSS is 432 MiB with jemalloc against 451 MiB
without (4 % lower). The reference Redis varied by 6 % between its two jobs for identical work, and throughput shows
no gain within a job. **The option stays off.** Neither build gives memory back after the delete (RSS stays at about
451 MiB; the reference releases 40 to 50 MiB), so the cause is not the allocator. Revisit after repeated runs and a
look at that retention.

Two later runs (#28, #29; see [Later runs](../jemalloc_benchmark.md#later-runs)) correct the first reading. The default
build's refill RSS is 450.6 to 451.3 MiB in every run; jemalloc's is 416.5 to 432.1 MiB, so it is 4 to 8 % lower,
and not "inside the noise" (the reference server's spread was the wrong yardstick). The option still stays off, as a
judgement: a gain of that size against an extra dependency and a second configuration to test. The CI job that built
and tested it was removed on 2026-11-25, so the allocator's tests no longer run in any workflow.

## Alternatives considered

- **Replace the global allocator (link jemalloc, change no code):** simplest, and covers connection buffers too. Not
  chosen: the counter would no longer be tied to the keyspace, and the whole process depends on the link order.
- **mimalloc or tcmalloc:** comparable gains; jemalloc is what Redis uses, so `INFO` fields and behaviour are known
  quantities. Revisit if jemalloc does not build in CI.
- **A traits parameter beside the allocator:** nothing but the allocator varies, so no (see DX-80).

## Consequences

- Easier: lower fragmentation under churn; sized frees (`sdallocx`) use the count `deallocate` already receives;
  `INFO` can later report jemalloc's `allocated` and `resident` through `mallctl`.
- Harder: the store depends on a non-standard API behind an `#ifdef`; a second allocator to test; two configurations
  in CI.
- Must be true: sanitizer presets build with the option off, because `je_mallocx` bypasses ASan's interception and
  would hide heap errors in the keyspace.
