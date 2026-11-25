# jemalloc keyspace allocator: measured results (TC-276, #50)

What `REDIS_USE_JEMALLOC` gained in memory and throughput, and what that means for the option's default
([ADR-0011](adr/0011-jemalloc-keyspace-allocator.md)). The method is in
[test_plan.md](test_plan.md) (TC-276); the performance comparison with Redis as a whole is in
[BEN-001](../../evaluation/benchmark_report.md).

## Conclusion

* The option stays **off by default**, as a judgement. Over three runs, the memory after a delete-then-refill is 4 to
  8 % lower with jemalloc, and that gain is real (see [Later runs](#later-runs)); throughput shows no gain that the
  same-job comparison supports. A gain of that size does not pay for an extra dependency (the vcpkg jemalloc port
  builds with autotools) and a second configuration to test, and the CI job that tested it was removed on
  2026-11-25. The first version of this document called the gain "inside the noise" of the reference server; that
  was the wrong yardstick, and is corrected below.
* Neither build returns memory to the operating system after a mass delete: RSS stays at about 451 to 454 MiB while
  `used_memory` falls to 57 to 61 MiB. The reference Redis gives back 40 to 50 MiB in the same test. That gap exists
  with either allocator, so it comes from somewhere other than the choice of allocator (see
  [Open questions](#open-questions)).

## Method

| Item | Value |
|------|-------|
| Run | Benchmark workflow, run #27 (id 44705693443), commit `50bb2e0`, branch `bench/memory`, 2026-11-20 |
| Jobs | `bench (gcc)` and `bench (gcc-jemalloc)`, both Release, image `gcc:16.2`; a runner with 4 CPUs each |
| Reference | Debian's `redis-server` 8.0.2, built with jemalloc 5.3.0, started by the same script in the same job |
| Memory | `scripts/bench_memory.sh`: 200,000 random keys per size; fill (16, 200, 2000-byte values), delete (most of the large keys, about 63 % of the medium ones), refill (700-byte values on new keys) |
| Throughput | `scripts/bench_server.sh`: 7 runs per scenario, median reported |

Every figure below is **one run** of each job; the two jobs ran on different machines.

## Memory

RSS is `VmRSS` of the server process. `used_memory` is what the server says it holds: for `redis_server` it is the
keyspace only, counted by the allocator (usable size with jemalloc, requested size without), for the reference server
every allocation. For that reason, compare RSS across servers and read the ratio only within one server.

| Server | Phase | RSS (MiB) | `used_memory` (MiB) | RSS / used |
|--------|-------|----------:|--------------------:|-----------:|
| `redis_server`, default allocator | empty | 9.8 | 0.0 | n/a |
| | fill | 451.2 | 414.8 | 1.09 |
| | delete | 451.2 | 56.9 | 7.93 |
| | refill | 451.2 | 180.5 | 2.50 |
| `redis_server`, jemalloc | empty | 10.5 | 0.0 | n/a |
| | fill | 454.5 | 428.7 | 1.06 |
| | delete | 453.5 | 61.0 | 7.43 |
| | refill | 432.1 | 195.7 | 2.21 |
| Reference Redis (gcc job) | fill | 445.9 | 415.8 | 1.07 |
| | delete | 401.6 | 44.5 | 9.03 |
| | refill | 386.3 | 181.7 | 2.13 |
| Reference Redis (jemalloc job) | fill | 446.9 | 416.7 | 1.07 |
| | delete | 410.3 | 44.4 | 9.25 |
| | refill | 410.3 | 181.4 | 2.26 |

Reading it:

* **Fill.** The two builds hold the same RSS (451.2 and 454.5 MiB). jemalloc's `used_memory` is 3 % higher because it
  counts usable size, so its ratio is lower (1.06 against 1.09) without any memory being saved.
* **Delete.** Neither build releases anything: RSS is unchanged at 451.2 and 453.5 MiB.
* **Refill.** jemalloc reuses the freed space better: RSS 432.1 MiB against 451.2 MiB (4.2 % lower), ratio 2.21
  against 2.50. This is the gain TC-276 looks for.
* **Spread.** The reference server ended the refill at 386.3 MiB in one job and 410.3 MiB in the other (6 % apart),
  for identical work. A 4 % difference cannot be told from that.

## Throughput

Median requests per second of `redis_server` (the reference server in brackets, same job).

| Scenario | Default allocator | jemalloc |
|----------|------------------:|---------:|
| `clients` (50 clients, `SET`) | 129,618 (129,366) | 161,812 (161,943) |
| `latency` (1 client) | 31,260 (30,266) | 73,855 (66,489) |
| `pipeline` (16 per batch) | 1,452,960 (1,331,780) | 1,683,856 (1,682,793) |
| `bigget` (`GET`, 2 M keys) | 1,103,144 (968,406) | 1,230,012 (1,031,194) |

The jobs ran on different runners: the reference server itself is 25 % faster on `clients` and 26 % on `pipeline` in
the jemalloc job. Across jobs the figures therefore show the machine, not the allocator. Within a job, `redis_server`
matches the reference on `clients` with either allocator, and on `pipeline` it is 9 % ahead with the default
allocator and level with the reference with jemalloc. On `latency` it is 3 % ahead (default) and 11 % ahead
(jemalloc), but that scenario varied from 61,350 to 85,324 across the jemalloc job's seven runs, so it settles
nothing.

## Later runs

Two more runs of the same script, on the same code path, change what can be said about the memory figures. The
default build's figures hardly vary between runs; the reference server's vary by several percent. A difference between
two builds must be judged against the builds' own spread, not the reference's.

RSS after the refill phase, in MiB:

| Run | Default allocator | jemalloc | Reference Redis |
|---|---:|---:|---|
| Benchmark #27 (2026-11-20) | 451.2 | 432.1 | 386.3 (gcc job), 410.3 (jemalloc job) |
| Benchmark #28 (2026-11-24, gcc job failed on the network) | none | 416.5 | 381.5 |
| Benchmark #29 (2026-11-25) | 451.3 (gcc), 450.6 (clang) | 421.4 | 402.5, 396.8, 376.9 |

- **The default build is deterministic:** 450.6 to 451.3 MiB in all three samples, and the same in the fill, delete
  and refill phases (it never gives anything back).
- **Every jemalloc sample is below every default sample,** by 4.2 % (432.1), 7.7 % (416.5) and 6.6 % (421.4). The
  jemalloc build's own spread is about 4 %, so the gain is real, and its size is between 4 and 8 %.
- **The reference varies from 377 to 410 MiB** for identical work, which is why comparing a 4 % gain with it said
  nothing either way.
- **Fill and delete:** jemalloc is 0.6 to 0.7 % higher at the fill (about 454.5 against 451.3) and keeps 452.5 to
  453.5 MiB after the delete, as the default does. Only the refill shows the gain.

## Threats to validity

* One run per job, on different shared runners. No figure here has a confidence interval.
* Different `used_memory` definitions (usable against requested size) make the ratio of the two builds not strictly
  comparable; RSS is the figure to trust.
* The refill phase writes one value size (700 bytes) into space freed from 200 and 2000-byte values. Other mixes may
  fragment differently.

## Open questions

* **Why does `redis_server` keep 451 MiB after the delete when the reference gives back 40 to 50 MiB?** It is the same
  with both allocators. Candidates: the hash table or its nodes are never shrunk; freed arenas are not trimmed
  (jemalloc's `dirty_decay_ms`, glibc's `malloc_trim`). To find out, repeat the run with `MALLOC_TRIM_THRESHOLD_` set
  and log jemalloc's `stats.allocated` and `stats.resident` through `mallctl`.
* **Is the gain real?** Answered in [Later runs](#later-runs): yes, 4 to 8 % with the refill phase of this script. Not
  tested: other value sizes, or a longer churn.

## Decision

`REDIS_USE_JEMALLOC` stays off by default. Revisit when the repeated runs show a refill gain outside the spread, or
when the delete-phase retention is explained. Recorded in ADR-0011.
