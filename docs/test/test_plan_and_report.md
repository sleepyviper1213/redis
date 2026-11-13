# Master Test Plan and Level Test Report

**Project:** Redis-compatible in-memory data store
**Document ID:** TEST-001
**Structure:** IEEE Std 829-2008 [1] (master test plan and level test report)

## Document control

| Date        | Revision | Author             | Change                                      |
|-------------|----------|--------------------|---------------------------------------------|
| 13 Nov 2026 | 1.0      | Khac Truong Nguyen | First version; replaces `TEST_CHECKLIST.md` |

## Table of contents

Part A: Test plan

1. [Introduction](#1-introduction)
2. [Test items](#2-test-items)
3. [Features to be tested](#3-features-to-be-tested)
4. [Features not to be tested](#4-features-not-to-be-tested)
5. [Approach](#5-approach)
6. [Item pass/fail criteria](#6-item-passfail-criteria)
7. [Suspension and resumption criteria](#7-suspension-and-resumption-criteria)
8. [Test deliverables](#8-test-deliverables)
9. [Environmental needs](#9-environmental-needs)
10. [Responsibilities and schedule](#10-responsibilities-and-schedule)
11. [Risks and contingencies](#11-risks-and-contingencies)
12. [Requirements traceability](#12-requirements-traceability)
13. [Use case test coverage](#13-use-case-test-coverage)

Part B: Level test report

14. [Overview](#14-overview)
15. [Test results](#15-test-results)
16. [Anomalies](#16-anomalies)
17. [Assessment](#17-assessment)
18. [References](#18-references)

---

# Part A: Test plan

## 1. Introduction

### 1.1 Purpose

This plan defines how the server is verified against the requirements in the Software Requirements Specification
(SRS) [2], and the design in the Software Design Description (SDD) [3]. Part B reports the results of the test run of
13 Nov 2026.

### 1.2 Scope

All functional requirements marked *Implemented* in the SRS (FR-001 to FR-019) and the non-functional requirements
that can be measured (NFR-001, NFR-003, NFR-004). Performance is measured in the benchmark report [4].

### 1.3 Definitions

| Term         | Meaning                                                                                      |
|--------------|----------------------------------------------------------------------------------------------|
| Test level   | A group of tests with one purpose and one environment: unit, integration, stress, and so on  |
| BDD          | Behaviour-driven style: each test is a `SCENARIO` with `GIVEN`, `WHEN`, `THEN` steps         |
| TC-nnn       | Identifier of an integration test case; UT-nnnn identifies a unit test case                  |
| Differential | Comparing this server's replies with real Redis' for the same commands                       |
| `[fork]`     | Tag for tests that need `fork()`; skipped on Windows                                         |

## 2. Test items

| Item                                  | Version                          | Tested at levels                       |
|---------------------------------------|----------------------------------|----------------------------------------|
| `redis_server` executable and library | Git commit `1f9e70c` and later   | Unit, integration, stress, differential |
| Snapshot and append-only file formats | Snapshot format version 2        | Unit, integration                      |
| Scripts in `scripts/`                 | Same commit                      | Used to run the levels above           |

## 3. Features to be tested

Each feature is identified by its requirement in the SRS and its use case [5]. Section 12 gives the tests for each.

| Features                                                                                        | Requirements          |
|-------------------------------------------------------------------------------------------------|-----------------------|
| String, list, hash, set and keyspace commands                                                   | FR-001 to FR-005      |
| Key expiry, lazy and by the periodic sweep                                                      | FR-006                |
| `PING`, `ECHO`, `INFO`, `CONFIG GET`, `CONFIG SET loglevel`                                     | FR-007                |
| RESP2 parsing, inline commands, pipelining, requests split across reads                         | FR-008                |
| Concurrent clients, one command at a time on the keyspace                                       | FR-009                |
| Error replies                                                                                   | FR-010                |
| Snapshots and the append-only file, including recovery from corrupt and truncated files         | FR-011, FR-012, FR-018 |
| Client limits                                                                                   | FR-014                |
| Logging, watchdog                                                                               | FR-015, FR-016        |
| Graceful shutdown                                                                               | FR-017                |
| Compatibility with Redis' replies                                                               | NFR-004               |

## 4. Features not to be tested

| Feature                                                    | Reason                                                      |
|------------------------------------------------------------|-------------------------------------------------------------|
| `AUTH`, Pub/Sub, transactions, Lua, replication, eviction  | Not implemented (FR-020 to FR-025)                          |
| `BGSAVE` and `BGREWRITEAOF` on Windows                     | Windows has no `fork`; the server replies with an error     |
| The POSIX crash report                                     | No automated test yet (G-3)                                 |
| Third-party libraries (Boost, spdlog, fmt, abseil, stdexec)| Assumed correct; they have their own tests                  |

## 5. Approach

### 5.1 Principles

* Test where the behaviour lives: in-process when no socket is needed, against a live server when one is.
* Compare with real Redis for compatibility, and check every reply against a model under load, because
  `redis-benchmark` proves only that *a* reply arrived.
* Run in random order, to expose tests that depend on each other.
* Use several compilers and sanitizers in CI.

### 5.2 Test levels

| Level           | Location                  | Runs against                  | Command                                      |
|-----------------|---------------------------|-------------------------------|----------------------------------------------|
| Unit            | `test/unit/`              | The code in-process           | `ctest --preset <preset>`                    |
| Integration     | `test/integration/`       | A running server, via hiredis | `scripts/test_server.sh <preset> <config>`   |
| Stress          | `test/stress/`            | A running server              | `scripts/stress_server.sh <preset> <config>` |
| Differential    | `scripts/diff_test.py`    | This server and real Redis    | `scripts/diff_server.sh <preset> <config>`   |
| Static analysis | clang-tidy, cppcheck      | The source                    | CI job `linux-clang-analysis`                |
| Performance     | `scripts/bench_server.sh` | This server and real Redis    | See [4]                                      |

**Unit tests** (`redis_unit_test`, 166 test cases) mirror `src/`: protocol (`common/resp`), keyspace
(`common/store`), commands, network, persistence, diagnostics and the foundation library (`core`).

**Integration tests** (`redis_integration_test`) cover `SAVE` (TC-001 to TC-006), `SET` (TC-007 to TC-014), `GET`
(TC-015 to TC-021), `DEL` (TC-022 to TC-027) and `PING` (UT-1002 and others).

**Stress test** (`redis_stress_test`) runs four phases and prints its seed so any failure replays exactly: (1) every
request split at every byte boundary, malformed framing, garbage; (2) many clients against local models, pipelined, with
large binary values and shared counters and sets checked for exact totals; (3) a final sweep that re-reads everything;
(4) several clients racing on one key, whose replies must match one order that respects each client's own order
(linearizability, itself tested by `redis_stress_check_test`).

**Differential test** sends one seeded, deterministic command stream (strings, lists, sets, hashes, keys) to both
servers and stops at the first reply that differs. It sends nothing that depends on time, so expiry is not covered.

### 5.3 Test design technique

Each test follows the use case's flows [5]: the main flow first, then each alternate flow (wrong arity, wrong type,
missing key, out of range, resource failure). Boundaries are tested at, below and above the limit.

## 6. Item pass/fail criteria

* A test case passes when every assertion in it holds. A level passes when all its test cases pass or are skipped
  for a stated reason.
* The stress and differential tests pass when they finish with no wrong reply.
* A release passes when the exit criteria in section 17.3 are met.

## 7. Suspension and resumption criteria

* **Suspend** testing of a level when its build fails, when the server cannot start, or when more than one test
  case in a row fails for an environmental reason (a port in use, a full disk).
* **Resume** after the cause is fixed and the failing level is rerun from the start.
* A test that fails under load but passes alone is recorded as an anomaly (section 16), not as a pass.

## 8. Test deliverables

| Deliverable                            | Location                                               |
|----------------------------------------|--------------------------------------------------------|
| This plan and report                   | `docs/test/test_plan_and_report.md`                    |
| Test code                              | `test/`                                                |
| Run scripts                            | `scripts/`                                             |
| CI definitions and logs                | `.github/workflows/ci.yml`; the Actions page           |
| Benchmark results                      | `bench.yml` artifacts; [4]                             |

## 9. Environmental needs

| Environment                  | Where             | Runs                                                        |
|------------------------------|-------------------|-------------------------------------------------------------|
| Linux, GCC 16.2, Debug       | GitHub Actions    | Unit, integration (`linux-gcc-debug`)                       |
| Linux, GCC 16.2, Release     | GitHub Actions    | Optimised build with LTO (`linux-gcc-release`)              |
| Linux, Clang 22, Debug       | GitHub Actions    | Unit, integration (`linux-clang-debug`)                     |
| Linux, GCC, ThreadSanitizer  | GitHub Actions    | Unit, integration (`linux-gcc-tsan`, privileged container)  |
| macOS, Homebrew LLVM         | GitHub Actions    | Unit, integration (`macos-arm64-llvm-debug`)                |
| Windows 11, Clang-cl         | Developer machine | Unit tests (Part B)                                         |

Tools: CMake with Ninja, vcpkg for the dependencies, Catch2 (test framework), hiredis (client), `redis-benchmark`
and `redis-server` (reference). `linux-gcc-asan` is switched off in CI until GCC 16.2 accepts stdexec's `let_value`
under `-fsanitize=address,undefined`.

## 10. Responsibilities and schedule

| Role                                 | Person             | Responsibility                                  |
|--------------------------------------|--------------------|-------------------------------------------------|
| Developer and tester                 | Khac Truong Nguyen | Writes and runs the tests, triages anomalies    |

Tests run in CI on every push to `main`, on every pull request, and on demand on `test/` and `bench/` branches. The
full plan is executed before each release.

## 11. Risks and contingencies

| Risk                                                      | Effect                          | Contingency                                      |
|-----------------------------------------------------------|---------------------------------|--------------------------------------------------|
| Timing-based tests miss their deadline on a busy machine  | False failures                  | Wider margins; rerun alone to confirm (A-1)      |
| No Linux machine for the developer                        | Linux bugs found late           | All Linux work goes through GitHub Actions       |
| Shared CI runners are noisy                               | Unreliable benchmark numbers    | Compare the two servers within one job only      |
| A sanitizer preset is broken by a compiler update         | Races or UB go unseen           | Keep TSan on; restore ASan (A-4)                 |

## 12. Requirements traceability

"Unit" paths are under `test/unit/`. A level marked *Gap* has no automated test.

| Requirement                     | Unit tests                                                                         | Other tests                                  | Result    |
|---------------------------------|------------------------------------------------------------------------------------|----------------------------------------------|-----------|
| FR-001 String commands          | `server/commands/string`, `set_ttl`                                                | Integration TC-007 to TC-021                 | Covered   |
| FR-002 List commands            | `server/commands/list`                                                             | Stress, differential                         | Covered   |
| FR-003 Hash commands            | `server/commands/hash`                                                             | Stress, differential                         | Covered   |
| FR-004 Set commands             | `server/commands/set`                                                              | Stress, differential                         | Covered   |
| FR-005 Keyspace commands        | `server/commands/keyspace`, `common/store/database`, `core/utils/glob`             | Integration TC-022 to TC-027; differential   | Covered   |
| FR-006 Key expiry               | `server/commands/set_ttl`, `core/chrono/deadline_timer`, `common/store/database`   | None (the differential test avoids time)     | Unit only |
| FR-007 Server commands          | `server/commands/info`, `config`                                                   | Integration `ping_pong`                      | Covered   |
| FR-008 TCP and protocol         | `common/resp/parser`, `request`, `server/net/pipeline`, `session`, `listener`      | Stress phase 1                               | Covered   |
| FR-009 Concurrent clients       | `server/net/executor`, `session`                                                   | Stress phases 2 and 4; TSan job              | Covered   |
| FR-010 Error replies            | `server/commands/reply`, `handler`, `server/net/net_error`                         | Stress phase 1; differential                 | Covered   |
| FR-011 Snapshots                | `server/persistence/snapshot`, `core/os/save_file`, `core/os/child_process`        | Integration TC-001 to TC-006 (see A-5)       | Unit only |
| FR-012 Append-only file         | `server/persistence/aof`                                                           | None                                         | Unit only |
| FR-013 Configuration            | None                                                                               | None                                         | **Gap** (A-2) |
| FR-014 Client limits            | `server/net/guard`, `session`                                                      | None                                         | Unit only |
| FR-015 Logging                  | `core/logging/lifecycle`, `server/commands/config`                                 | None                                         | Covered   |
| FR-016 Diagnostics              | `server/diagnostics/command_watchdog`, `core/stdexec_ext/watchdog`                 | None for the POSIX crash report              | Partly (A-3) |
| FR-017 Graceful shutdown        | `server/net/listener` (`[shutdown]`)                                               | None                                         | **Gap** (A-6) |
| FR-018 Startup safety           | `server/persistence/snapshot`, `aof` (corrupt, truncated, unsafe manifest)         | None                                         | Unit only |
| FR-019 Benchmarking             | None                                                                               | `bench.yml` on `bench/` branches             | By CI     |
| NFR-001 Performance             | None                                                                               | Benchmark report [4]                         | Measured  |
| NFR-003 Reliability             | `core/os/save_file`, `persistence/snapshot`, `aof`                                 | None                                         | Unit only |
| NFR-004 Compatibility           | `server/commands/*`                                                                | Differential test                            | Covered   |
| NFR-007 Portability             | None                                                                               | CI matrix; Windows unit run                  | Partly    |

## 13. Use case test coverage

| Use case                           | Verified by                                                                                         |
|------------------------------------|-----------------------------------------------------------------------------------------------------|
| UC-001 Save snapshot               | `snapshot` (round trip, corrupt and truncated files, background save `[fork]`); TC-001 to TC-006 (hidden, A-5) |
| UC-002 Set key-value pair          | `string`, `set_ttl` (options and invalid expiry); TC-007 to TC-014 (empty and binary values, 1 MB value, missing arguments) |
| UC-003 Get key value               | `string`; TC-015 to TC-021 (missing key, overwrite, empty and binary values, missing arguments)     |
| UC-004 Delete keys                 | `keyspace`; TC-022 to TC-027 (twice, several keys, no argument, binary name)                        |
| UC-005 Ping                        | `handler`; integration `ping_pong`                                                                  |
| UC-006 Connect over TCP            | `session`, `pipeline`, `listener`, `guard`; stress phase 1                                          |
| UC-007 Strings and counters        | `string` (INCR and DECR, several keys, APPEND, wrong type, integer parsing)                         |
| UC-008 to UC-010 Lists, hashes, sets | `list`, `hash`, `set`; stress model                                                               |
| UC-011 Expire keys                 | `set_ttl`, `deadline_timer`, `database`                                                             |
| UC-012 Inspect the keyspace        | `keyspace`, `glob`                                                                                  |
| UC-013 Rewrite the append-only file | `aof` ("Rewriting the log", `[fork]`); no test of a failing or cancelled rewrite                   |
| UC-014 Inspect the server          | `info`, `config`                                                                                    |
| UC-015 Change the log level        | `config` ("Changing the log level at runtime", "CONFIG SET is off unless enabled"), `lifecycle`     |
| UC-016 Configure and start         | None for option parsing (A-2); `snapshot` and `aof` cover loading and refusing to start             |
| UC-017 Stop the server             | `listener` (`[shutdown]`); the final save and exit status are not tested (A-6)                      |
| UC-018 Watch the logs              | `command_watchdog`, `watchdog`                                                                      |
| UC-019 Extend system features      | Not a testable flow                                                                                 |

---

# Part B: Level test report

## 14. Overview

This report covers one run of the unit test level on a developer machine, plus the status of the other levels. The
other levels run in CI and were not run for this report.

| Item                  | Value                                                                            |
|-----------------------|----------------------------------------------------------------------------------|
| Date                  | 13 Nov 2026                                                                      |
| Binary                | Prebuilt `redis_unit_test.exe`, Clang-cl, RelWithDebInfo, built the same day     |
| Source                | Commit `1f9e70c` plus the uncommitted working tree                               |
| Machine               | Windows 11, developer desktop, other programs running                            |
| Order                 | Catch2 default (random)                                                          |

## 15. Test results

### 15.1 Unit level

| Measure           | Run 1 | Run 2 |
|-------------------|------:|------:|
| Test cases        | 166   | 166   |
| Passed            | 160   | 159   |
| Failed            | 2     | 3     |
| Skipped           | 4     | 4     |
| Assertions        | 1787  | 1787  |
| Assertions passed | 1785  | 1781  |

* **Skipped (4):** tests tagged `[fork]`, for example "Saving in the background". Windows cannot fork. They run on
  Linux and macOS in CI.
* **Failed:** timer-based tests in `core/stdexec_ext/watchdog.test.cpp` and `every.test.cpp`, where a callback was
  expected within a few milliseconds and had not run. The failing set differed between the runs.
* **Rerun of those tests alone** (`[watchdog],[every]`; 10 test cases, 74 assertions): all passed, 3 runs of 3.

### 15.2 Other levels

| Level                     | Status in this report                                                                   |
|---------------------------|-----------------------------------------------------------------------------------------|
| Integration               | Not run. Runs on Linux and macOS in CI                                                  |
| Stress, differential      | Not run. Run on Linux with `scripts/stress_server.sh` and `scripts/diff_server.sh`      |
| Static analysis           | Not run. CI job `linux-clang-analysis`                                                  |
| Performance               | See [4]                                                                                 |

## 16. Anomalies

| ID  | Description                                                                                                  | Severity | Recommended action                                           |
|-----|--------------------------------------------------------------------------------------------------------------|----------|--------------------------------------------------------------|
| A-1 | Timer-based tests (`watchdog`, `every`) fail under load and pass alone                                       | Low      | Widen the time margins, or wait on a signal instead of a clock |
| A-2 | No automated test of option parsing and of precedence among command line, config file and environment (FR-013) | Medium  | Add tests that run the app with each source                  |
| A-3 | The POSIX crash report is not tested (FR-016)                                                                | Low      | Crash a child on purpose and check the log                   |
| A-4 | `linux-gcc-asan` is off in CI                                                                                | Medium   | Re-enable when the GCC and stdexec problem is fixed          |
| A-5 | Integration `save` tests TC-001 to TC-006 carry the hidden tag `[.]`, and `scripts/test_server.sh` runs the binary with no tag, so they never run by default | Medium | Run them explicitly (`"[save]"`), or fix them and remove the tag |
| A-6 | No end-to-end test of `SIGTERM`: final save, exit status 0, non-zero exit when the final save fails (FR-017)  | Medium   | Script a test that starts, writes, signals and inspects the snapshot |
| A-7 | Append-only file and client limits are tested in-process only                                                | Low      | Add integration tests for an `--appendonly` restart and `--maxclients` |
| A-8 | Expiry (FR-006) has no live-server test; the differential test avoids time                                   | Low      | Add integration tests with short `PX` expiries               |

A-5 was found while preparing this report and is unconfirmed: the tag was read from the source, and the tests were not
run.

## 17. Assessment

### 17.1 Results against the plan

No functional unit test failed. The 2 and 3 failures in the unit run are timing-sensitive tests that pass alone (A-1).
The Windows run cannot assess the `fork`-based features, and the integration, stress and differential levels were not
run, so this report alone does not show the release criteria are met.

### 17.2 Coverage

FR-013 and FR-017 have no automated test that exercises the requirement itself (A-2, A-6). Six requirements (FR-006,
FR-011, FR-012, FR-014, FR-018 and NFR-003) are covered by unit tests only, with no test on a live server (section 12).

### 17.3 Exit criteria for release

* [ ] All CI jobs are green on the release commit (Linux GCC and Clang, TSan, macOS).
* [ ] The stress and differential tests pass on Linux with `--io-threads 1` and with `--io-threads 4`.
* [ ] No open anomaly above Low severity (A-2, A-4, A-5 and A-6 closed).
* [ ] Every requirement marked Implemented in the SRS has an automated test in section 12.
* [ ] The benchmark figures in the README match the latest benchmark report.

## 18. References

[1] *IEEE Standard for Software and System Test Documentation*, IEEE Std 829-2008, 2008.

[2] K. T. Nguyen, "Software Requirements Specification," `docs/analysis/software_requirement_specification.md`, 2026.

[3] K. T. Nguyen, "Software Design Description," `docs/design/software_design_document.md`, 2026.

[4] K. T. Nguyen, "Benchmark and Evaluation Report," `docs/evaluation/benchmark_report.md`, 2026.

[5] K. T. Nguyen, "Use Case Descriptions," `docs/analysis/use_cases/README.md`, 2026.
