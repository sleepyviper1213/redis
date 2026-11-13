# Use Case: Extend System Features

## Use Case ID

UC-019

## Description

Operator, acting as a developer of the server, adds a feature, such as a new command or one of the planned features
(Pub/Sub, transactions, authentication), without changing how existing commands behave. This is the business rule
BR-009 shown on the use case diagram.

## Primary Actor

Operator (a maintainer or contributor building the server from source)

## Preconditions

* The source builds and its tests pass on CI (Linux, macOS, Windows)
* The feature is described by a functional requirement in the SRS (for example FR-020 to FR-025, Planned)

## Postconditions

* The new command is available to clients, with Redis' reply format and error wording
* Existing tests still pass; new tests cover the feature
* The SRS, and a use case here, describe it

## Flow of Events

| Step | Actor    | Description                                                                                       |
|------|----------|---------------------------------------------------------------------------------------------------|
| 1    | Operator | Adds a handler and a `CommandInfo` entry (name, arity, whether it writes) to a command table in `src/server/commands/` |
| 2    | Operator | Marks it as a write command when it changes the keyspace, so it is logged to the append-only file |
| 3    | Operator | Adds unit tests, and integration tests against a running server                                   |
| 4    | Operator | Builds and runs the tests on CI                                                                   |
| 5    | Server   | Dispatches the new command by name, checks its arity, and times it for `INFO commandstats` and the watchdog |

## Alternate Flows

### A1: Non-deterministic or relative writes

* A command whose replay would differ (a random choice like `SPOP`, a relative expiry like `EXPIRE`) logs a
  deterministic equivalent to the append-only file instead of itself

### A2: Feature beyond one command

* Features such as Pub/Sub or transactions change the connection's state machine, not only the command table; they
  need a design first (see the state and sequence diagrams)

### A3: Performance regression

* A change that slows the server is caught by running redis-benchmark on a `bench/` branch (FR-019)

## Related Functional Requirements

* FR-020 to FR-025: Planned features
* FR-019: Benchmarking

## Related Nonfunctional Requirements

* NFR-006: Maintainability
* NFR-008: Extensibility
* NFR-009: Reusability

## Notes

* Unknown commands already get Redis' `ERR unknown command` reply, so a client probing for a feature that is not there
  gets a clear answer.
