# Extensibility: Design Choices

Choices specific to this feature. System-wide design stays in the
[SDD](../../design/software_design_document.md); a decision that was a real trade-off has an
[ADR](../../design/adr/). Rows are added when the feature changes.

| #        | Choice | Why | Where recorded |
|----------|--------|-----|----------------|
| DX-66 | Table-driven dispatch: a command is one `CommandInfo` row (name, function, arity, write flag) and one function | A new command is one row and one function | SDD §9, §5.1, BR-009, NFR-008, UC-019 |
| DX-67 | One table and one source file per command family (`string`, `keyspace`, `list`, `set`, `hash`, `server`) | Families stay separate (from code) | SDD §5.1, `src/server/commands/` |
| DX-68 | `CommandHandler` merges the family tables into one hash table at construction; an optional `extra` span is merged last and wins a name clash | Registration needs no change in the dispatcher (from code) | SDD §5.1, `src/server/commands/handler.cpp` |
| DX-69 | A command is `resp::Value (*)(CommandContext &, Args)`; the context lends it the database, persistence, the append-only file, statistics and the guard | A command sees only what it may act on | SDD §10.3, `src/server/commands/command.hpp` |
| DX-70 | The dispatcher checks arity before the call: non-negative means exact, negative means at least `-n` | Handlers never repeat the check | SDD §10.3, `src/server/commands/handler.cpp` |
| DX-71 | The name is upper-cased into a fixed 32-byte buffer; a longer name matches nothing and gets `ERR unknown command` | No allocation per lookup (from code); a client probing for a missing feature gets a clear answer | UC-019 Notes, `src/server/commands/handler.cpp` |
| DX-72 | The `write` flag marks commands that change the keyspace so they are logged to the append-only file; relative or random writes log a deterministic equivalent | A replay must reproduce the state | UC-019 step 2 and A1, FR-012 |
| DX-73 | The dispatcher records and times every command, so a new command shows in `INFO commandstats` and the watchdog without extra code | Observability needs no per-command work | UC-019 step 5, SDD §11.1 step 4 |
| DX-74 | Failures are error replies built with the `reply::*` helpers, in Redis' wording | Clients see the errors they know | SDD §14.5, FR-010, `src/server/commands/command.hpp` |
| DX-75 | FR-020 to FR-025 (authentication, Pub/Sub, transactions, Lua, replication and clustering, memory limit and eviction) | Not designed yet | SDD §17 |

## Code map

* `src/server/commands/command.hpp`: `CommandInfo`, `CommandFn`, `CommandContext`, `reply::*`.
* `src/server/commands/handler.hpp`, `src/server/commands/handler.cpp`: `CommandHandler`, dispatch.
* `src/server/commands/string.cpp`, `src/server/commands/keyspace.cpp`, `src/server/commands/list.cpp`, `src/server/commands/set.cpp`, `src/server/commands/hash.cpp`, `src/server/commands/server.cpp`: one table per family.
* `src/server/commands.hpp`: the family accessors.
* `src/server/commands/args.hpp`: `Args`.

## Open questions

* Features beyond one command (Pub/Sub, transactions) change the connection's state machine and need a design first (UC-019 A2); design notes for transactions and retries are in [TODO.md](../../../TODO.md).
* Allocation-free dispatch by a compile-time perfect hash: see [TODO.md](../../../TODO.md) (Performance (SET)).
