# Use Case: Change the Log Level

## Use Case ID

UC-015

## Description

Client raises or lowers the log level of a running server with `CONFIG SET loglevel`, to look closer at it without
restarting it and losing its state.

## Primary Actor

Client (redis-cli, used by an operator)

## Preconditions

* Server is running, and was started with `--enable-config-set`
* Command syntax is valid: `CONFIG SET loglevel <level>`, where `<level>` is one of `trace`, `debug`, `info`,
  `warning`, `error`, `critical` or `off`

## Postconditions

* Every log channel uses the new level from the next message on
* The change is logged at warning level, so it is on record at any level short of `error`
* `CONFIG GET loglevel` reports the new level

## Flow of Events

| Step | Actor  | Description                                                             |
|------|--------|-------------------------------------------------------------------------|
| 1    | Client | Sends `CONFIG SET loglevel debug`                                       |
| 2    | Server | Checks that `CONFIG SET` is enabled and that the parameter is `loglevel` |
| 3    | Server | Sets the level on every log channel                                     |
| 4    | Server | Logs `Log level changed to debug by CONFIG SET`                         |
| 5    | Server | Replies `+OK`                                                           |

## Alternate Flows

### A1: `CONFIG SET` not enabled

* Server replies: `ERR CONFIG SET is disabled; start the server with --enable-config-set to allow it`

### A2: Another parameter

* Server replies: `ERR Unsupported CONFIG parameter: <name>`

### A3: Unknown level

* Server replies: `ERR Invalid argument '<level>' for CONFIG SET 'loglevel'`; the level is unchanged

## Related Functional Requirements

* FR-007: Server commands
* FR-015: Logging

## Related Nonfunctional Requirements

* NFR-002: Security (off by default)
* NFR-006: Maintainability

## Notes

* There is no authentication (FR-020 is planned), so once `--enable-config-set` is given, any client that can reach
  the port can change the level. Leave it off unless the port is private.
* The level does not survive a restart; set `--log-level` for that ([UC-016](uc_016_configure_and_start.md)).
