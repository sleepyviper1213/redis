# Feature: Key Expiry

| Item         | Value |
|--------------|-------|
| Feature ID   | FEAT-EXP |
| Status       | Implemented; documented retroactively (17 Nov 2026) |
| Requirements | FR-006.1 to FR-006.11, NFR-010 (defined in the [SRS](../../analysis/software_requirement_specification.md#33-functional-requirements); not repeated here) |
| Issue / epic | [#30](https://github.com/sleepyviper1213/redis/issues/30); all issues: [issues.md](issues.md) |

A key can be given a lifetime. Once its deadline passes it is gone for every command, and a periodic sweep removes
it from memory. Deadlines survive a restart.

This folder holds everything about the feature that may change together. Stable documents (SRS, SDD, global use
cases) are only referenced.

## Documents in this folder

| Document | Contents |
|----------|----------|
| [use_cases/](use_cases/) | The use cases and their acceptance criteria (AC-nn), Given / When / Then |
| [design.md](design.md) | Design choices for this feature, with links to the ADRs and SDD sections |
| [adr/](adr/) | Decisions: ADR-0005 |
| [test_plan.md](test_plan.md) | Test scenarios TC-nn, levels, timing rules |
| [issues.md](issues.md) | Generated list of the GitHub issues labelled `feat:key_expiry` (not edited by hand) |
| [finding_45_set_past_deadline.md](finding_45_set_past_deadline.md) | Spike #45: `SET` with a past `EXAT`/`PXAT` in Redis 8.0.2; decision: follow Redis (#48) |
| [traceability.md](traceability.md) | Acceptance criteria ↔ use case ↔ requirement ↔ test, and how each NFR is verified |
| [diagrams/](diagrams/) | State, sequence and activity diagrams |

## Use cases

| Use case | Actor | Acceptance criteria | What it specifies |
|----------|-------|---------------------|-------------------|
| [UC-011 Expire keys](use_cases/uc_011_expire_keys.md) | Client | AC-01, 02, 04, 05, 06 | Set a deadline, read the time left, cancel it, which writes keep it |
| [UC-002 Set key-value pair](../data_types/use_cases/uc_002_set_key_value.md) (in [data_types](../data_types/README.md)) | Client | AC-03 | `SET` with `EX`, `PX`, `EXAT`, `PXAT`, `KEEPTTL` |
| [UC-020 Remove expired keys](use_cases/uc_020_remove_expired_keys.md) | Server | AC-07, 08 | A due key is gone at once; removal on access and by the sweep |
| [UC-021 Keep deadlines across restarts](use_cases/uc_021_keep_deadlines_across_restarts.md) | Operator | AC-09 | Snapshot and append-only file keep absolute deadlines |
| [UC-022 Monitor key expiry](use_cases/uc_022_monitor_key_expiry.md) | Operator | AC-10 | `expired_keys` and `expires` in `INFO` |

UC-002 lives in [data_types](../data_types/README.md) because `SET` is a general command; only its expiry
options (AC-03) belong to this feature.

## Diagrams

* Lifecycle of a key: [state_key_expiry_lifecycle.puml](diagrams/state_key_expiry_lifecycle.puml)
* Set, read and removal: [sequence_key_expiry.puml](diagrams/sequence_key_expiry.puml)
* The sweep: [activity_expiry_sweep.puml](diagrams/activity_expiry_sweep.puml)
* Use cases overview (global): [use_case.puml](../../analysis/diagrams/use_case.puml)

## Bugs, unimplemented features and limits by design

**Bug** (tracked, tested as expected to fail until fixed):

* #40: `DEL` removes a due key without first checking its deadline, so it replies `:1` for a key that other commands
  already see as missing. Redis replies `:0`.

**Not implemented** (tracked):

* #41: the `EXPIRE` family takes no `NX`, `XX`, `GT` or `LT` option.
* #42: there are no `EXPIRETIME` or `PEXPIRETIME` commands.

**By design** (changing one is a change to this feature):

* LI-05: a key past its deadline stays in memory until it is accessed or swept, so `DBSIZE` and `expires` may count
  it, as in Redis. This includes a key set by `SET` with a past `EXAT` or `PXAT` (#48, fixed;
  [finding](finding_45_set_past_deadline.md)); the `EXPIRE` family deletes at once.
* Deadlines use the wall clock, since `EXPIREAT`, `PXAT` and snapshots name calendar times. A clock set backwards
  extends lifetimes, and one set forwards shortens them.
* Only one database exists, so expiry has no database dimension.
* Expiry is not replicated; there is no replication (SRS section 1.2.2).

How each is tested is in [test_plan.md](test_plan.md).
