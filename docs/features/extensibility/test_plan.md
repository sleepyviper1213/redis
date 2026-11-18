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
