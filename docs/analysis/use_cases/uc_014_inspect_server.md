# Use Case: Inspect the Server

## Use Case ID

UC-014

## Description

Client reads the server's state and settings: statistics with `INFO`, and a few settings with `CONFIG GET`, which
clients such as redis-benchmark probe when they start.

## Primary Actor

Client (redis-cli, redis-benchmark, a monitoring agent)

## Preconditions

* Server is running
* Command syntax is valid: `INFO [<section> ...]`, `CONFIG GET <pattern>`

## Postconditions

* Nothing changes on the server

## Flow of Events

| Step | Actor  | Description                                                                                   |
|------|--------|-----------------------------------------------------------------------------------------------|
| 1    | Client | Sends `INFO`                                                                                  |
| 2    | Server | Collects the default sections: `server`, `clients`, `memory`, `stats`, `persistence`, `keyspace` |
| 3    | Server | Replies with one bulk string: a `# Name` line per section, then `field:value` lines           |

## Alternate Flows

### A1: Chosen sections

* `INFO clients stats` replies with only those sections; names are case-insensitive
* `INFO all` or `INFO everything` adds `commandstats`: calls and time per command
* `INFO default` gives the default sections; an unknown section gives an empty reply, as in Redis

### A2: Read settings (`CONFIG GET`)

* Server replies with an array of name, value pairs for the settings whose name matches the glob pattern:
  `appendonly`, `appendfsync`, `dbfilename`, `dir`, `loglevel` and `save` (always empty)
* A pattern that matches none gives an empty array

### A3: Unsupported `CONFIG` form

* Anything but `CONFIG GET <pattern>` and `CONFIG SET loglevel <level>` gets
  `ERR CONFIG supports only 'CONFIG GET <pattern>' and 'CONFIG SET loglevel <level>'`

## Related Functional Requirements

* FR-007: Server commands
* FR-014: Client limits (`connected_clients`, `rejected_connections`)

## Related Nonfunctional Requirements

* NFR-004: Compatibility (field names as in Redis, so existing tools parse them)
* NFR-006: Maintainability (observability)

## Notes

* Fields per section:

| Section       | Fields                                                                                                   |
|---------------|----------------------------------------------------------------------------------------------------------|
| `server`      | `uptime_in_seconds`                                                                                      |
| `clients`     | `connected_clients`, `maxclients`                                                                        |
| `memory`      | `used_memory`, `used_memory_human`                                                                       |
| `stats`       | `total_connections_received`, `rejected_connections`, `total_commands_processed`, `instantaneous_ops_per_sec`, `expired_keys`, `keyspace_hits`, `keyspace_misses` |
| `persistence` | `rdb_bgsave_in_progress`, `rdb_last_save_time`, `aof_enabled`; with the append-only file, `aof_rewrite_in_progress`, `aof_fsync_policy` |
| `keyspace`    | `db0:keys=<n>,expires=<n>`, when the keyspace is not empty                                               |
| `commandstats`| `cmdstat_<name>:calls=<n>,usec=<n>,usec_per_call=<n>`                                                    |

* Changing a setting is [UC-015](uc_015_change_log_level.md); everything else is set at startup
  ([UC-016](uc_016_configure_and_start.md)).
