# Benchmark and Evaluation Report

**Project:** Redis-compatible in-memory data store
**Document ID:** BEN-001
**Structure:** Level test report after IEEE Std 829-2008 [1] (overview, test results, anomalies, assessment)

## Document control

| Date        | Revision | Author             | Change                                                         |
|-------------|----------|--------------------|----------------------------------------------------------------|
| 13 Nov 2026 | 1.0      | Khac Truong Nguyen | First version. Figures are those recorded in the README; no benchmark was rerun for this revision |

## Table of contents

1. [Overview](#1-overview)
2. [Method](#2-method)
3. [Results](#3-results)
4. [Anomalies and threats to validity](#4-anomalies-and-threats-to-validity)
5. [Assessment](#5-assessment)
6. [How to reproduce](#6-how-to-reproduce)
7. [References](#7-references)

---

## 1. Overview

### 1.1 Purpose

This report evaluates the performance requirement NFR-001 [2]: how close the server comes to Redis in throughput,
measured with the same tool under the same conditions. It also records how far the figures can be trusted.

### 1.2 Items under test

| Item                        | Description                                                                          |
|-----------------------------|--------------------------------------------------------------------------------------|
| This server                 | `redis_server`, Release build (GCC and Clang on Linux; LLVM on macOS)                |
| Reference                   | Redis, as `redis-server`: Homebrew's on macOS, Debian's package on the Linux runners |
| Tool                        | `redis-benchmark`, run by `scripts/bench_server.sh`                                  |

### 1.3 Summary

On the measured scenarios this server delivers between about 94 % and 108 % of Redis' throughput for plain `SET`
traffic, and a higher figure for pipelined and random-key `GET` traffic in four of the six comparisons of that kind.
The differences are small enough that run-to-run noise on shared machines can explain some of them (section 4).

## 2. Method

### 2.1 Scenarios

`scripts/bench_server.sh` runs four scenarios:

| Scenario   | Load                                                                                   | What it exercises                          |
|------------|----------------------------------------------------------------------------------------|--------------------------------------------|
| `clients`  | 50 parallel clients, no pipelining, `SET`                                              | The default `redis-benchmark` run          |
| `latency`  | 1 client, one request at a time, `SET`                                                 | Per-request overhead                       |
| `pipeline` | 50 clients, 16 commands per batch, `SET`                                               | Command execution cost                     |
| `bigget`   | `GET` over a keyspace of 2,000,000 random keys, 16 per batch                           | Cost of cache misses; the other scenarios touch one key |

### 2.2 Controls

* **Alternating order.** The two servers take turns on every scenario, and the order flips each round, so a noisy
  machine disturbs both alike.
* **Same conditions.** The reference runs on its own port with persistence off and a throwaway directory, so it
  starts empty like this server. This server runs with `--log-level error`, since logging would skew the numbers.
* **Repeats.** Each scenario is repeated `RUNS` times. The script prints every run, the **best** run (the least
  disturbed by other processes) and the **median** (the steadier figure).
* **CPU pinning (CI).** `SERVER_CPUS=0-1` and `CLIENT_CPUS=2-3`, so the load generator cannot take the server's CPUs.
* **Duration.** `redis-benchmark` times a run in 250 ms ticks when it uses threads, so a run must last seconds; the
  pipeline and `bigget` scenarios use a larger request count (`PIPE_REQUESTS`, 8,000,000 by default).

### 2.3 Environments

| Environment            | Hardware                   | Settings recorded in the README                                  |
|------------------------|----------------------------|------------------------------------------------------------------|
| macOS                  | Apple Silicon (M-series)   | `RUNS=7 REQUESTS=500000`; best of 7 runs                         |
| Linux CI, GCC          | GitHub runner, 4 CPUs      | `RUNS=3 REQUESTS=200000`, 1 client thread; best of 3 runs        |
| Linux CI, Clang        | GitHub runner, 4 CPUs      | As above                                                         |

The Linux workflow (`.github/workflows/bench.yml`) is run on demand by pushing to a `bench/` branch.

## 3. Results

Figures are requests per second (rps). "Ratio" is this server divided by Redis, from the same job.

### 3.1 macOS, Apple Silicon

| Scenario                     | This server  | Redis        | Ratio  |
|------------------------------|-------------:|-------------:|-------:|
| 50 clients, `SET`            | 98,348       | 104,254      | 94 %   |
| 1 client, serial             | 35,577       | 35,251       | 101 %  |
| Pipelined (`-P 16`)          | 1,377,411    | 1,422,475    | 97 %   |
| `bigget` (see section 4)     | 1,060,445    | 823,045      | 129 %  |

### 3.2 Linux CI, GCC

| Scenario                     | This server  | Redis        | Ratio  |
|------------------------------|-------------:|-------------:|-------:|
| 50 clients, `SET`            | 115,340      | 120,700      | 96 %   |
| 1 client, serial             | 30,883       | 29,958       | 103 %  |
| Pipelined (`-P 16`)          | 1,724,138    | 1,328,904    | 130 %  |
| `bigget`                     | 1,089,918    | 907,029      | 120 %  |

### 3.3 Linux CI, Clang

| Scenario                     | This server  | Redis        | Ratio  |
|------------------------------|-------------:|-------------:|-------:|
| 50 clients, `SET`            | 84,818       | 86,281       | 98 %   |
| 1 client, serial             | 34,495       | 32,031       | 108 %  |
| Pipelined (`-P 16`)          | 1,307,190    | 1,351,351    | 97 %   |
| `bigget`                     | 1,307,190    | 909,091      | 144 %  |

### 3.4 Observations

* **Unpipelined `SET` with 50 clients:** 94 % to 98 % of Redis on all three setups. The gap is small and consistent in
  sign.
* **One client, one request at a time:** 101 % to 108 %. Round-trip time dominates, so the two servers behave alike.
* **Pipelined:** 97 % on macOS and with Clang, 130 % with GCC. The two Linux builds disagree by 33 points on this
  scenario, which is larger than any other difference between them.
* **`bigget`:** 120 % to 144 % in every setup. The README attributes this to zero-copy argument parsing and writing
  replies straight into the output buffer. That is the design intent ([SDD](../design/software_design_document.md),
  decision D4); no experiment here isolates it.
* **Absolute numbers** are not comparable between rows: the hardware, request counts and compilers differ. Only the
  ratio within one row is meaningful.

## 4. Anomalies and threats to validity

| ID  | Issue                                                                                                            | Effect on the conclusions                                                  |
|-----|------------------------------------------------------------------------------------------------------------------|---------------------------------------------------------------------------|
| B-1 | The figures are best-of-N. The script also prints medians, but the README shows only the best                    | Favours whichever server had one lucky run; medians should be reported     |
| B-2 | No variance, confidence interval or number of runs per figure other than N is given                              | Differences of a few percent cannot be called real                         |
| B-3 | The README labels the fourth scenario "Large-value `GET`"; the script's `bigget` is `GET` over 2,000,000 random keys, 16 per batch | The label does not match the workload. Either the label or the scenario should change |
| B-4 | `bench.yml` now defaults to `THREADS=2`, `RUNS=7`, and the README's Linux figures say 1 client thread and `RUNS=3`  | The Linux figures may predate the current defaults; they should be regenerated |
| B-5 | Shared CI runners are noisy                                                                                      | Only the ratio inside one job means anything                               |
| B-6 | One version of Redis per platform, not stated in the README                                                      | Results depend on it; record `redis-server --version` with the figures     |
| B-7 | GCC and Clang results on the same Linux scenario differ widely (130 % against 97 % pipelined)                    | The cause is unexplained; do not generalise either figure                  |
| B-8 | Only `SET` and `GET` are in the scripted scenarios; the README also suggests other commands with `redis-benchmark -t` | Other command families are not evaluated here                       |
| B-10| Under Clang, this server's pipelined and `bigget` figures are identical (1,307,190 rps), and `bench_server.sh` notes that identical rates across runs and servers mean the runs were too short | These two Clang figures may be an artefact of the 250 ms timing ticks; treat them as unreliable until rerun with longer runs |
| B-9 | Only throughput is reported; no latency percentiles, memory use or scaling with `--io-threads`                   | NFR-001's latency target is not shown by this data                         |

## 5. Assessment

### 5.1 Against NFR-001

The data support the claim that the server is **comparable to Redis** on these workloads: within about 6 % on
unpipelined `SET`, and ahead on pipelined and random-key `GET` in most setups. They do not support a claim that it is
faster than Redis in general. The lead on `bigget` is the most consistent result (all three setups), and is the one
most worth investigating.

### 5.2 Confidence

Moderate for the unpipelined `SET` ratios (three setups agree), low for the pipelined ratios (the two Linux builds
disagree), and unknown for everything else until medians and run counts are published (B-1, B-2).

### 5.3 Recommended actions

1. Regenerate all figures with the current `bench.yml` defaults and publish the median and the range of runs next to
   the best (B-1, B-2, B-4).
2. Rename the fourth scenario in the README, or add a true large-value `GET` scenario (B-3).
3. Record the Redis version and the runner type with the figures (B-6).
4. Add latency percentiles and a run with `--io-threads 4` (B-9).
5. Investigate the GCC and Clang difference on the pipelined scenario (B-7).

## 6. How to reproduce

```sh
# Linux or macOS; needs redis-benchmark and, for the comparison, redis-server
$ REF_SERVER=redis-server RUNS=7 REQUESTS=500000 THREADS=2 \
      scripts/bench_server.sh linux-gcc Release

# Pin the server and the load generator to separate CPUs (Linux)
$ SERVER_CPUS=0-1 CLIENT_CPUS=2-3 REF_SERVER=redis-server scripts/bench_server.sh linux-gcc Release

# Several network threads
$ SERVER_ARGS="--io-threads 4" scripts/bench_server.sh linux-gcc Release

# On GitHub Actions: push any commit to a bench/ branch
$ git push -f origin HEAD:bench/<name>
```

The workflow uploads one result file per compiler (`bench` artifact) and writes it to the job summary.

## 7. References

[1] *IEEE Standard for Software and System Test Documentation*, IEEE Std 829-2008, 2008.

[2] K. T. Nguyen, "Software Requirements Specification," `docs/analysis/software_requirement_specification.md`, 2026.

[3] K. T. Nguyen, "Software Design Description," `docs/design/software_design_document.md`, 2026.

[4] Redis Ltd., "Redis benchmark," https://redis.io/docs/latest/operate/oss_and_stack/management/optimization/benchmarks/.
