# Use Case Descriptions

One file per use case of the [use case diagram](../diagrams/use_case.puml). The files live with their feature, in
`docs/features/<feature>/use_cases/` ([feature index](../../features/README.md)); this page is the index of all of them. Requirements (FR, NFR, BR) are defined in
the [Software Requirements Specification](../software_requirement_specification.md).

These use cases are Appendix A of the SRS, which follows IEEE Std 830-1998 (see its section 1.4). IEEE 830 does not
prescribe a use case template; the one below is used throughout.

## Actors

| Actor    | Who                                                                                              |
|----------|--------------------------------------------------------------------------------------------------|
| Client   | Anything that speaks RESP or inline commands over TCP: redis-cli, client libraries, redis-benchmark, telnet |
| Operator | Whoever runs, configures, monitors and extends the server: a person, a service manager, CI       |
| Server   | The server acting on its own, on a timer or while serving a command (UC-020)                    |

## Use cases

| ID                                                   | Use case                         | Actor    | Commands / trigger                                       | Requirements           |
|------------------------------------------------------|----------------------------------|----------|----------------------------------------------------------|------------------------|
| [UC-001](../../features/persistence/use_cases/uc_001_save_snapshot.md)                    | Save snapshot                    | Client   | `SAVE`, `BGSAVE`, `LASTSAVE`                             | FR-011                 |
| [UC-002](../../features/data_types/use_cases/uc_002_set_key_value.md)                    | Set key-value pair               | Client   | `SET`                                                    | FR-001, FR-006         |
| [UC-003](../../features/data_types/use_cases/uc_003_get_key_value.md)                    | Get key value                    | Client   | `GET`                                                    | FR-001                 |
| [UC-004](../../features/keyspace/use_cases/uc_004_delete_keys.md)                      | Delete key(s)                    | Client   | `DEL`, `FLUSHDB`                                         | FR-005                 |
| [UC-005](../../features/connection/use_cases/uc_005_ping.md)                             | Client connection ping-pong      | Client   | `PING`, `ECHO`                                           | FR-007, FR-014         |
| [UC-006](../../features/connection/use_cases/uc_006_connect.md)                          | Connect over TCP                 | Client   | TCP connect, RESP2 or inline requests                    | FR-008, FR-009, FR-014 |
| [UC-007](../../features/data_types/use_cases/uc_007_update_strings_and_counters.md)      | Update strings and counters      | Client   | `MSET`, `MGET`, `APPEND`, `STRLEN`, `INCR`, `DECR`, `INCRBY`, `DECRBY` | FR-001   |
| [UC-008](../../features/data_types/use_cases/uc_008_use_lists.md)                        | Store and read lists             | Client   | `LPUSH`, `RPUSH`, `LPOP`, `RPOP`, `LLEN`, `LRANGE`, `LINDEX` | FR-002             |
| [UC-009](../../features/data_types/use_cases/uc_009_use_hashes.md)                       | Store and read hashes            | Client   | `HSET`, `HGET`, `HMGET`, `HDEL`, `HGETALL`, `HLEN`, `HEXISTS` | FR-003            |
| [UC-010](../../features/data_types/use_cases/uc_010_use_sets.md)                         | Store and read sets              | Client   | `SADD`, `SREM`, `SCARD`, `SMEMBERS`, `SISMEMBER`, `SINTER`, `SPOP` | FR-004       |
| [UC-011](../../features/key_expiry/use_cases/uc_011_expire_keys.md)                      | Expire keys                      | Client   | `EXPIRE`, `PEXPIRE`, `EXPIREAT`, `PEXPIREAT`, `TTL`, `PTTL`, `PERSIST` | FR-005, FR-006, NFR-010 |
| [UC-012](../../features/keyspace/use_cases/uc_012_inspect_keyspace.md)                 | Inspect the keyspace             | Client   | `EXISTS`, `KEYS`, `TYPE`, `DBSIZE`                       | FR-005                 |
| [UC-013](../../features/persistence/use_cases/uc_013_rewrite_aof.md)                      | Rewrite the append-only file     | Client   | `BGREWRITEAOF`                                           | FR-012                 |
| [UC-014](../../features/observability/use_cases/uc_014_inspect_server.md)                   | Inspect the server               | Client   | `INFO`, `CONFIG GET`                                     | FR-007                 |
| [UC-015](../../features/observability/use_cases/uc_015_change_log_level.md)                 | Change the log level             | Client   | `CONFIG SET loglevel`                                    | FR-007, FR-015         |
| [UC-016](../../features/server_lifecycle/use_cases/uc_016_configure_and_start.md)              | Configure and start the server   | Operator | Options, config file, `REDIS_*` environment              | FR-013, FR-018         |
| [UC-017](../../features/server_lifecycle/use_cases/uc_017_stop_server.md)                      | Stop the server                  | Operator | `SIGINT`, `SIGTERM`                                      | FR-017                 |
| [UC-018](../../features/observability/use_cases/uc_018_watch_logs.md)                       | Watch the logs                   | Operator | Log file, watchdog warnings, crash reports               | FR-015, FR-016         |
| [UC-019](../../features/extensibility/use_cases/uc_019_extend_features.md)                  | Extend system features           | Operator | New commands and planned features                        | BR-009, FR-020–FR-025  |
| [UC-020](../../features/key_expiry/use_cases/uc_020_remove_expired_keys.md)              | Remove expired keys              | Server   | Lazy removal on access; sweep every 100 ms               | FR-006, NFR-010        |
| [UC-021](../../features/key_expiry/use_cases/uc_021_keep_deadlines_across_restarts.md)   | Keep deadlines across restarts   | Operator | Snapshot, append-only file, restart                      | FR-006, FR-011, FR-012, NFR-010 |
| [UC-022](../../features/key_expiry/use_cases/uc_022_monitor_key_expiry.md)               | Monitor key expiry               | Operator | `INFO stats`, `INFO keyspace`                            | FR-006, FR-007, NFR-010 |

## Writing a use case

Each file has the same sections, in this order: Use Case ID, Description, Primary Actor, Preconditions,
Postconditions, Flow of Events (a table of steps for the main success path), Alternate Flows (`A1`, `A2`, ...),
Related Functional Requirements, Related Nonfunctional Requirements, Notes. A use case with acceptance criteria
(AC-nn, Given / When / Then) adds an Acceptance Criteria section before Notes.

* Name the file `uc_<id>_<short_name>.md`, put it in the folder of its feature, and add it to the table above and to the diagram.
* Quote replies and log messages exactly as the server writes them.
* IDs are never reused or renumbered; a use case that goes away is marked as withdrawn here.
