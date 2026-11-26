# Extensibility: Test Scenarios

Test plan for [UC-019 Extend system features](use_cases/uc_019_extend_features.md) (BR-009, FR-020 to FR-025). It
extends the [Master Test Plan](../../test/test_plan_and_report.md), whose section 13 calls UC-019 "not a testable
flow".

Scenario IDs are `TC-nnn`, one sequence for the whole project ([ID ranges](../README.md#id-ranges)). Coverage was matched by test names in `test/unit/server/commands/`, not by reading every
assertion.

## Scope

The planned features (FR-020 to FR-025) are not implemented, so no test is written for them. What can be tested today
is the mechanism UC-019 relies on: a command is a `CommandInfo` entry (name, function, arity, write flag) in a table;
the handler takes extra tables, so a test can register a command without touching the built-in ones. A new command is
proven registered and dispatched by the scenarios below, which a contributor repeats for their own command.

| Mark    | Meaning                                                                          |
|---------|----------------------------------------------------------------------------------|
| Unit    | A unit test whose scenario name matches; named in the *Existing test* column     |
| Partial | A test covers part of the scenario                                               |
| Gap     | No test found                                                                    |

## Mechanism

| ID        | Behaviour | Given / When | Expected | Level | Existing test | Coverage |
|-----------|-----------|--------------|----------|-------|---------------|----------|
| TC-253 | A command in an extra table is registered and dispatched | `CommandHandler` built with a table holding `OOMTEST`; execute it | The command runs and its reply is appended after earlier replies of the batch | Unit | `handler`: "A command that runs out of memory" (table `EXTRA`) | Unit |
| TC-254 | Built-in commands still work beside an extra table | The same handler; `PING` after the new command | `+PONG`; the batch continues | Unit | `handler`: "A command that runs out of memory" | Unit |
| TC-255 | Name lookup ignores case; an unknown name is `ERR unknown command` | `get`, `GeT`; `NoSuchCmd` | Dispatched; unknown name echoed back as sent | Unit | `set_ttl`: "Command names are matched without regard to case" | Unit |
| TC-256 | Arity is checked before the function is called | A new command called with too few or too many arguments | `-ERR wrong number of arguments for '<name>' command` for the extra command too | Unit | `set_ttl`: "Malformed requests get RESP error replies" (built-in `GET` only) | Partial |
| TC-257 | A reply is appended to the caller's buffer | Two requests run against one buffer holding an earlier reply | Replies follow in order, nothing lost | Unit | `set_ttl`: "A reply is appended to the caller's buffer" | Unit |
| TC-258 | A new command is timed for `INFO commandstats` and the watchdog | Run the new command; `INFO commandstats` | `cmdstat_<name>:calls=1,usec=...`; the watchdog sees it as the running command | Unit | `info`: "INFO counts lookups and times each command" (built-in commands only) | Partial |
| TC-259 | A write command is logged to the append-only file, relative or random ones as an equivalent (A1) | A table entry with `write = true`; `--appendonly` | The log holds the command, or its deterministic form | Unit | `aof`: "Write commands are logged", "Commands that cannot be replayed as received" (built-in commands) | Partial |
| TC-260 | Existing behaviour does not change | The whole suite, and the differential and stress tests, after a change | All pass | Unit, Integration, Stress, Differential | CI runs them on every change (master test plan section 5) | Unit |

## Sender algorithms

The reusable algorithms NFR-008 names for new background work. Tests in `test/unit/core/stdexec_ext/`.

| ID        | Behaviour | Given / When | Expected | Level | Existing test | Coverage |
|-----------|-----------|--------------|----------|-------|---------------|----------|
| TC-309 | The backoff delay doubles from the initial delay and stops at the cap (DX-87) | `retry_policy::backoff_delay` with initial 100 ms, cap 1 s, for failures 1 to 5, 1000 and `SIZE_MAX`; an initial delay above the cap; and with a zero initial delay | 100, 200, 400, 800, 1000 ms; 1000 ms after 1000 and `SIZE_MAX` failures (no overflow); the cap from the first; zero stays zero | Unit | `retry`: "The backoff delay grows to its cap" | Unit |
| TC-310 | `retry_policy::exponential_backoff` waits between attempts | An attempt that succeeds on its third try; initial 20 ms, cap 1 s, 5 attempts | Completes with the attempt's value after 3 calls, no sooner than 60 ms (20 + 40) | Unit | `retry`: "A retry with backoff waits longer after each failure" | Unit |
| TC-311 | It gives up after `max_attempts`, with no wait after the last failure | An attempt that always fails; 1 attempt, initial 10 s | The attempt's error after 1 call, well within 10 s | Unit | `retry`: "A retry with backoff waits longer after each failure" | Unit |
| TC-312 | A stop request during the wait ends the retry as stopped, with no further attempt | An attempt that always fails; initial 10 s; the retry raced against a 100 ms `time_limited` | The time limit's value, after 1 call, well within 10 s | Unit | `retry`: "A retry with backoff waits longer after each failure" | Unit |
| TC-313 | A stop that comes during an attempt which ignores it, and which then fails, starts no further attempt | An attempt that requests a stop and then throws; policy `immediately(3)`, and `exponential_backoff` with a 10 s initial delay | The retry is stopped after 1 call, the error is dropped; with backoff, well within 10 s. `immediately` relies on `repeat_until`'s trampoline checking the stop token before each round | Unit | `retry`: "A stop during an attempt that ignores it ends the retry" | Unit |
| TC-314 | A jitter source chooses each backoff wait, the first included (DX-87) | An attempt that fails twice then succeeds; initial 400 ms, cap 10 s; a jitter source that records its `upper` and returns `upper / 100` | It is asked with 400 then 800 ms, and the retry takes no less than 12 ms (4 + 8) and well under the 1.2 s the unjittered waits would | Unit | `retry`: "A retry with jittered backoff waits what the jitter source chooses" | Unit |
| TC-315 | `full_jitter` draws uniformly from `[0, upper]` | `full_jitter` over a fixed-seed engine; 10 000 draws with `upper` 100 ms; and `upper` 0 | Every draw within `[0, 100]` ms, with draws in both the lowest and the highest tenth; zero gives zero | Unit | `retry`: "Full jitter draws from zero to the backoff delay" | Unit |
| TC-316 | `fail_after` fails work that runs past its limit (DX-88) | Work that waits 10 s on a timer and honours stop; `fail_after(100 ms)` | `set_error` with `std::errc::timed_out`, no exception thrown; well within 10 s; the work saw the stop request | Unit | `watchdog`: "fail_after() fails work that runs past its limit" | Unit |
| TC-317 | Work that ends in time passes through unchanged | Work that sends 42 at once; work that fails at once with `connection_refused`; limit 10 s | 42, and `connection_refused`, each well within 10 s (the timer is stopped, not waited out) | Unit | `watchdog`: "fail_after() fails work that runs past its limit" | Unit |
| TC-318 | `on_expiry` forces work that a stop cannot end | Work that ignores stop and ends only when `on_expiry` releases it; limit 100 ms | `on_expiry` runs once, after the limit and before the work ends; the race completes with `timed_out` | Unit | `watchdog`: "fail_after() fails work that runs past its limit" | Unit |
| TC-319 | A hung attempt is retried under backoff | An attempt that hangs on its first try and succeeds on its second; `fail_after(100 ms)` inside `make_attempt`; `exponential_backoff` with 3 attempts | Completes with the value after 2 calls; the policy saw one `system_error` with `timed_out` | Unit | `retry`: "A hung attempt fails after its limit and is retried" | Unit |

## Planned features

| Requirement | Planned feature | Testable now | Scenarios |
|-------------|-----------------|--------------|-----------|
| BR-009 | Extensibility for additional features | Only through the mechanism above | TC-253 to 260 |
| FR-020 | Authentication and permissions | Not testable yet; scenarios to be written when the feature is specified | None |
| FR-021 | Pub/Sub | Not testable yet; scenarios to be written when the feature is specified | None |
| FR-022 | Transactions (`MULTI`, `EXEC`, `WATCH`) | Not testable yet; scenarios to be written when the feature is specified | None |
| FR-023 | Lua scripting | Not testable yet; scenarios to be written when the feature is specified | None |
| FR-024 | Clustering or replication | Not testable yet; scenarios to be written when the feature is specified | None |
| FR-025 | Memory limit and eviction | Not testable yet; scenarios to be written when the feature is specified | None |

Until then a client that sends one of these commands gets `-ERR unknown command` (TC-255).

Coverage per use case is in [traceability.md](traceability.md).

## Entry and exit criteria

* **Entry**: the unit suite runs in the chosen preset.
* **Exit**: a new command ships with scenarios equivalent to TC-253, 256, 258 and 259; no Gap above stays open
  without an entry in the test plan's anomaly list (section 16).
