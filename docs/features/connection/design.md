# Connection: Design Choices

Choices specific to this feature. System-wide design stays in the
[SDD](../../design/software_design_document.md); a decision that was a real trade-off has an
[ADR](../../design/adr/). Rows are added when the feature changes.

| #        | Choice | Why | Where recorded |
|----------|--------|-----|----------------|
| DX-34 | Network I/O on `--io-threads` threads; exactly one thread (the keyspace thread) executes command batches | Each command is atomic with no locks; I/O still scales with threads | [ADR-0001](adr/0001-single-threaded-command-executor.md), SDD §14.1, §14.2, FR-009 |
| DX-35 | With `--io-threads 1` the main thread does both I/O and execution; the batch runs inline | Matches Redis; the single-thread server pays nothing for the hand-off (from code: the executor is threaded only when `io_threads > 1`) | SDD §14.1, §11.1 step 3, `src/server/redis_server.cpp` |
| DX-36 | A session is a sender pipeline: read, parse, execute, write | Structured cancellation and no frame per request | [ADR-0002](adr/0002-stdexec-senders-over-callbacks.md), SDD D2 |
| DX-37 | The accept loop and the session round loop are coroutines; the rest of the session stays senders | The loops branch per connection; a coroutine for the whole session would allocate a frame per call and turn errors into exceptions | [ADR-0003](adr/0003-coroutines-only-for-branching-loops.md), SDD D3 |
| DX-38 | A batch is everything one read delivers; pipelined requests run in order and are answered with one write | Pipelining throughput; an ordering guarantee to the client | SDD §11.1, §11.3, UC-006 A1 |
| DX-39 | The RESP parser is incremental: it returns the bytes a request used or "incomplete"; inline commands are framed too; a client-declared length is bounded before anything is allocated | A split frame waits for more bytes; a hostile length cannot force a large allocation | SDD §5.1, FR-008 |
| DX-40 | A malformed request is answered `ERR Protocol error: ...`, then the connection is closed; earlier requests in the batch are still answered | Redis behaviour (SDD §10.1); the byte stream cannot be resynchronised (from code) | SDD §10.1, UC-006 A3 |
| DX-41 | Limits are one `NetGuard` that counts a `Violation` per kind: `--maxclients` (an admission ticket), query buffer, output buffer, `--idle-timeout`, `--request-timeout` | One place decides and counts; violations show in `INFO` | SDD §14.4, FR-014, `src/server/net/guard.hpp` |
| DX-42 | A client over `--maxclients` is told `ERR max number of clients reached`, best effort and without blocking, then dropped | Redis behaviour; a full server must not block on a rejected client | UC-006 A2, `src/server/net/detail/admission.hpp` |
| DX-43 | `--request-timeout` races "execute and write" against a timer, as the idle limit races the read | Frees the server from a client that stops reading its replies; not a Redis setting | `src/server/net/net_limits.hpp`, [TODO.md](../../../TODO.md) (per-request timeouts), SDD §12 |
| DX-44 | A session failure is `Isolate` (ends that client only); a failed accept loop is `FailFast`; connection errors are `Failure<ConnectionKind>` with one `display` | A bad client cannot end the server; a log line and an `ERR` reply share wording | SDD §14.3, §14.5, [ADR-0009](../../design/adr/0009-errors-as-values.md) |
| DX-81 | A session's decisions (what a read means, the buffer limits, how a timeout or failure ends it) are a socket-free `Conversation`; the session only moves bytes and hops schedulers | Decisions testable from strings in both modes, without a socket or a clock; no virtual calls or templates added to the hot path | [ADR-0012](adr/0012-session-decisions-apart-from-io.md) (Accepted), TC-278 to TC-290; TC-291 to TC-294 pending |

## Code map

* `src/server/net/listener.hpp`, `src/server/net/detail/accept_loop.cpp`, `src/server/net/detail/admission.cpp`: accept loop and admission.
* `src/server/net/detail/session.cpp`, `src/server/net/detail/pipeline.cpp`: the session and its request pipeline.
* `src/server/net/detail/conversation.hpp` (ADR-0012): the session's decisions, apart from its I/O.
* `src/server/net/executor.cpp`: the keyspace thread (inline when not threaded).
* `src/server/net/guard.hpp`, `src/server/net/net_limits.hpp`: `NetGuard`, `Violation`, `NetLimits`.
* `src/server/net/net_error.hpp`: `ConnectionKind`, `ListenKind`.
* `src/common/resp/parser.cpp`, `src/common/resp/request.cpp`: RESP and inline framing.
* `src/server/commands/server.cpp`: `PING`, `ECHO`.
* Diagrams: [state_connection_lifecycle.puml](diagrams/state_connection_lifecycle.puml), [concurrency_threads.puml](diagrams/concurrency_threads.puml).

## Open questions

* Per-client state for `MULTI`/`WATCH`, and cleanup when a session ends: see [TODO.md](../../../TODO.md) (Transactions).
* Bound the work one batch may hold the keyspace thread: see [TODO.md](../../../TODO.md) (Executor hand-off and fairness).
* SDD §17: no authentication (FR-020); bind to a private address.
