# Server Lifecycle: Design Choices

Choices specific to this feature. System-wide design stays in the
[SDD](../../design/software_design_document.md); a decision that was a real trade-off has an
[ADR](../../design/adr/). Rows are added when the feature changes.

| #         | Choice | Why | Where recorded |
|-----------|--------|-----|----------------|
| DX-56 | Every option is settable on the command line, in a `--config` file or in a `REDIS_*` environment variable, declared once with CLI11 | One declaration serves all three sources (from code: `->envname(...)` per option) | SDD §8.3, FR-013, `src/app/setup_cli_options.cpp` |
| DX-57 | An unknown option or a value that fails its check is reported on stderr and the server exits non-zero before starting | Fail before any state is touched | UC-016 A1, FR-013 |
| DX-58 | Startup order: logging and crash handler, load the append-only files or else the snapshot, bind, start threads and housekeeping, log "Ready to accept connections" | Data is loaded before any client can connect | SDD §11.2, [activity_startup.puml](diagrams/activity_startup.puml) |
| DX-59 | A load or bind failure is fatal (`ServerKind::LoadFailed`, `ListenFailed`); the server never starts empty | A later save would overwrite the only copy | [ADR-0008](adr/0008-refuse-to-start-on-unreadable-data.md), SDD D8, FR-018 |
| DX-60 | `RedisServer::run()` races the accept loop, housekeeping and the signal wait; the first to finish cancels the others by stop token | Structured cancellation; the shutdown race is a few lines | [ADR-0002](../connection/adr/0002-stdexec-senders-over-callbacks.md), SDD §14.3, FR-017 |
| DX-61 | Accept loop and housekeeping are `FailFast`; sessions are `Isolate` and are drained through a counting scope outside the race; an I/O thread failure requests stop | A server without its accept loop is of no use; one client's failure must not end it | SDD §14.3, `src/core/stdexec_ext/supervise.hpp` |
| DX-62 | Shutdown order: stop accepting and housekeeping, drain sessions, wait for the executing batch, cancel `BGSAVE` and an unfinished rewrite, close the append-only file, write the final snapshot | The final save sees the state after the last command, and a running `BGSAVE` would make it fail (from code comment) | SDD §11.2, FR-017, UC-017 |
| DX-63 | A failed final save exits non-zero (`ServerKind::FinalSaveFailed`) and leaves the previous snapshot | A supervisor sees the failure; the server cannot resume once its event loop has stopped | UC-017 A1, `src/server/server_error.hpp` |
| DX-64 | Lifecycle errors are `Failure<ServerKind>` that carry the stage's own error as source; the wording is in one place | One source of wording for log and exit | [ADR-0009](../../design/adr/0009-errors-as-values.md), SDD §14.5 |
| DX-65 | Dependencies and toolchains are pinned and the CMake presets are layered | Reproducible builds on every platform | [ADR-0010](../../design/adr/0010-pinned-vcpkg-and-layered-presets.md), NFR-007 |
| DX-76 | After a `FailFast` end the server attempts the final snapshot; if that fails, it exits non-zero | Keeps the writes since the last save when the keyspace thread is still healthy; a dead accept loop does not block the save | UC-017 A2, SDD §11.2, FR-017 |
| DX-78 | Test seam: `ServerConfig::fail_housekeeping_after`, set from the environment variable `REDIS_TEST_FAIL_HOUSEKEEPING_AFTER` (not a command line option), makes housekeeping throw after that many ticks | The accept loop retries its errors, so no real condition ends the server through FailFast; TC-262 needs one | TC-262, `src/app/main.cpp`, `src/server/redis_server.cpp` |

## Code map

* `src/app/main.cpp`, `src/app/setup_cli_options.cpp`: options, logging and crash-handler setup, building the server.
* `src/server/redis_server.cpp`: `RedisServer::run()`, signals, the supervised race, shutdown and the final save.
* `src/server/server_error.hpp`: `ServerKind`, `ServerError`.
* `src/server/services.hpp`: what the server owns and wires together.
* `src/core/stdexec_ext/supervise.hpp`: `supervised`, `FailPolicy`.
* Diagram: [activity_startup.puml](diagrams/activity_startup.puml).

## Open questions

* Test that a `FailFast` failure ends the server (TC-262). The final snapshot after a FailFast end is decided: attempt it, and exit non-zero if it fails (DX-76). Not yet implemented.
* Check and state the precedence of command line, environment and `--config`: see [TODO.md](../../../TODO.md) (Documentation and submission); the use cases deliberately claim no order.
* SDD §17: on Windows there is no crash handler and `BGSAVE` is unavailable.
