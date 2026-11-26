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
| DX-87 | `retry()` is the one retry algorithm; its ready-made policies are in `retry_policy`, named as policies so a call reads `retry(attempt, retry_policy::...)`: `immediately(n)` (was `retry_times`) and `exponential_backoff(scheduler, max_attempts, initial, cap[, jitter])`, plain arguments rather than a limits struct; `cap` takes the type of `initial`, so `100ms` and `5s` mix. After failure *n* the backoff policy waits `backoff_delay(initial, cap, n) = min(cap, initial × 2^(n-1))` on a timed scheduler, and gives up after `max_attempts` attempts with no last wait. Jitter is opt-in: a callable `Duration(Duration upper)` passed as the third argument chooses each wait from `backoff_delay(...)`, the first wait included; `retry_policy::full_jitter(engine)`, which owns its engine, draws it uniformly from `[0, upper]`. Without one the wait is exactly `backoff_delay(...)` | Background work that retries a failing resource gets a growing delay without writing its own policy, and `retry()` stays policy-free. Many clients that fail together and back off deterministically retry in lockstep and overload the server again; full jitter spreads them, and jittering the first wait too matters because that retry is the most synchronised. Opt-in, because a local resource (the accept loop's descriptors) has no herd; the random source is passed in so tests can fix it | NFR-008, `src/core/stdexec_ext/retry.hpp` |
| DX-88 | `work \| fail_after(scheduler, limit[, on_expiry])` races `work` against `limit` as `time_limited` does, but past the limit completes with `set_error(std::errc::timed_out)`, an error code, instead of a substitute value; `work`'s own values and errors pass through. The optional `on_expiry()` runs as soon as the limit passes, before the race waits for `work` to unwind. For a retried operation that may hang it goes inside `make_attempt`, so each attempt gets its own limit | A hang never fails, so `retry()` would wait on it for ever; this turns it into a failure the policy sees and can tell apart (a `system_error` with `timed_out`). An error code, not a throwing `on_expiry`, because exceptions are kept for crashes. `when_any` waits for the stopped work to unwind, so no attempt outlives its race; `on_expiry` is for work a stop request cannot end (a Windows send the peer does not read), which it forces, for example by closing the socket. Wrapped around the whole `retry()` instead, a limit bounds all attempts together and ends with no further one | NFR-008, `src/core/stdexec_ext/watchdog/fail_after.hpp` |
| DX-75 | FR-020 to FR-025 (authentication, Pub/Sub, transactions, Lua, replication and clustering, memory limit and eviction) | Not designed yet | SDD §17 |

## Code map

* `src/server/commands/command.hpp`: `CommandInfo`, `CommandFn`, `CommandContext`, `reply::*`.
* `src/server/commands/handler.hpp`, `src/server/commands/handler.cpp`: `CommandHandler`, dispatch.
* `src/server/commands/string.cpp`, `src/server/commands/keyspace.cpp`, `src/server/commands/list.cpp`, `src/server/commands/set.cpp`, `src/server/commands/hash.cpp`, `src/server/commands/server.cpp`: one table per family.
* `src/server/commands.hpp`: the family accessors.
* `src/server/commands/args.hpp`: `Args`.
* `src/core/stdexec_ext/retry.hpp`: `retry`; `retry_policy::immediately`, `exponential_backoff`, `backoff_delay`, `full_jitter` (DX-87).
* `src/core/stdexec_ext/watchdog/fail_after.hpp`: `fail_after` (DX-88), beside `time_limited.hpp`.

## Open questions

* Features beyond one command (Pub/Sub, transactions) change the connection's state machine and need a design first (UC-019 A2); design notes for transactions and retries are in [TODO.md](../../../TODO.md).
* Allocation-free dispatch by a compile-time perfect hash: see [TODO.md](../../../TODO.md) (Performance (SET)).
