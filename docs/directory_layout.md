# Directory Layout

Each directory under `src/` is one library target. Headers live next to their
sources and are included by their module-qualified path
(`"core/resp/parser.hpp"`), because the include root is `src/`. Dependencies
point one way only:

```text
redis/
├── cmake/                 Build modules and presets
│   ├── presets/           Layered CMake presets (base → options → toolchain → platform)
│   └── sanitizers/        One file per sanitizer build configuration
├── docs/                  Requirements, use cases and design diagrams
├── scripts/               Run the server; run integration tests against it
├── src/
│   ├── core/              redis_core: no networking, no server state
│   │   ├── resp/          RESP parser/encoder; request framing (RESP + inline)
│   │   ├── error/         std::expected helpers (REDIS_TRY, FAILED)
│   │   ├── logging/       Channels, async spdlog setup, RAII shutdown guard
│   │   └── utils/         Glob matching (KEYS), small helpers
│   └── server/            redis_server_lib + the redis_server executable
│       ├── storage/       Typed values and the keyspace, with expiry indexes
│       ├── commands/      One file per command family + table-driven dispatch
│       ├── persistence/   Snapshot format, SAVE/BGSAVE, POSIX process and fsync
│       └── net/           Event loop, TCP accept loop, per-client sessions
└── test/                  Mirrors src/
    ├── core/              Unit tests for redis_core
    ├── server/            Storage, command and persistence tests, in-process
    ├── integration/       hiredis tests against a running server
    └── stress/            Verifying stress test against a running server
```

```text
net ──► commands ──► storage
 │         └───────► persistence ──► storage
 └──────────────────────────────────► core (RESP, logging, utils)
```

`core` never depends on `server`. Within `server`, `storage` depends on
nothing else, so the keyspace can be tested without commands, sockets or
files.

## Request path

1. `net/listener` accepts a connection and starts a `net/detail/session`
   sender for it. The session lives in that sender's operation state, so
   nothing else owns or shares it.
2. The hot path is split in stages (`net/pipeline`). The session reads bytes
   and `parse_pending` cuts complete requests out of them: RESP arrays from
   client libraries, or inline commands from telnet and redis-benchmark. A
   request can be split across reads, and one read can carry several
   pipelined ones. These stages touch only the client's own buffer, so they
   run on whichever I/O thread owns the socket.
3. `execute_batch` runs the parsed requests. It touches the keyspace, so with
   `--io-threads` above 1 the session hands the batch to `net/executor`: one
   thread, reached through stdexec's scheduler and resumed back on the
   session's own strand, that is the only one executing commands. With
   `--io-threads 1` there is no hand-off: `execute_pending` runs both stages
   inline, as plain functions kept out of the session's sender chain.
4. `commands/handler` checks the request shape and arity, then calls the
   command through a hash table built from each family's `CommandInfo` list.
5. The command reads or writes `storage/database` through typed lookups
   (`find_as<List>`, `find_or_create<Set>`), which return `WrongType` instead
   of a value of another type.
6. Replies to every request in the batch are RESP-encoded and sent back in one
   write, again from the I/O thread.

### Sessions

A session and the accept loop are stdexec senders over Asio's `use_sender`
(`net/detail/session`, `net/listener`); the hot path stages live in
`net/detail/pipeline`.

| | |
|---|---|
| Session state lives in | the chain's operation state, with no coroutine frame |
| Round | `read \| parse \| execute \| write`, repeated by `repeat_until` |
| Hop to the keyspace thread | `Executor::run_on`, then `continues_on` the strand's `AsioContext` |
| Owns the connections | a `stdexec::counting_scope` |
| Shutdown | sessions are cancelled through the scope's stop token and unwound before the loop stops |

`AsioContext` adapts an Asio executor (a strand) into a stdexec scheduler,
which stdexec does not ship. The accept loop is a coroutine
(`stdexec_ext::InlineTask`) that `co_await`s `stdexec_ext::retry` over one
`async_accept`, rebuilt for each attempt so every client gets its own strand.
A coroutine is used for loops with branches that run per accept or failure, not
per request: the request path stays a sender chain, because a coroutine
allocates a frame per call and turns `set_error` into an exception. The
housekeeping timer below is a coroutine for the same reason.

### What a failure stops

`RedisServer::run()` runs three tasks under one `exec::when_any`: the accept
loop, the housekeeping timer and the wait for SIGINT/SIGTERM. The first to
finish, however it finishes, cancels the other two through the stop token.
Each task's `stdexec_ext::FailPolicy` is named where it is started
(`supervised<...>`):

| Task | Policy | A failure |
|---|---|---|
| A session | `Isolate` | is logged and ends that client only |
| The accept loop | `FailFast` | is logged, then ends the server with that error |
| Housekeeping | `FailFast` | ends the server with that error |
| An I/O thread | (outside the tasks) | requests stop, then ends the server with that error |

Sessions are not in the race, since they come and go: once it is over they are
cancelled through the listener's scope and drained, then the event loop stops.
After a signal the final snapshot is written; after a failure `run()` returns
the error without writing one, and `main()` exits non-zero.

### What a client can rely on

What a session reads at once, every complete request in it, is one batch.
The keyspace thread executes a batch whole, a command at a time, and nothing
is checked between its commands, so:

- **Each command is applied entirely or not at all.** Nothing interrupts a
  command, or the batch it is in, once it has started.
- **A batch stops part-way only at the output limit.** Once its replies pass
  `--client-output-buffer-limit`, the commands after the one that crossed it
  do not run; the earlier ones have, and the client is dropped without any
  reply.
- **Cancellation is honoured between operations only.** A stop at shutdown,
  the idle limit (`--timeout`, which only ever interrupts the wait for a
  request) and the request limit (`--request-timeout`) all act the same way:
  a batch still queued for the keyspace thread never runs, a batch already
  running completes, and an unfinished reply is dropped with the client.
  With one I/O thread a batch runs as soon as it is read and is never
  queued, so only the reply can be cut short there.
- **A reply received is durable as configured.** With an append-only file,
  a batch's writes are in the file (and on disk, with `appendfsync always`)
  before any of its replies is sent.

What a client cannot know is what happened when its connection drops, or is
closed by a limit, before the reply arrives: its commands may have run in
full, or not at all. Retrying is safe for idempotent commands (`SET`, `DEL`);
retrying `INCR` or `LPUSH` blindly may apply it twice. See "Retries and
idempotency" in `TODO.md`.

## Background work

`net/redis_server` runs a 100 ms timer (Redis' `serverCron`) on the event
loop; each tick executes on the executor thread, like any command, because it
touches the keyspace. Each tick removes up to 1000 expired keys, oldest deadline first, and
collects a finished `BGSAVE` child with a non-blocking `waitpid`.

`BGSAVE` forks. The child serialises its copy-on-write view of the keyspace
and exits with `_exit`; the parent keeps serving. All POSIX calls live in
`persistence/posix.cpp`.

## Tests

| Kind          | Location                         | Runs with                                  |
|---------------|----------------------------------|--------------------------------------------|
| Unit          | `test/core`, `test/server`       | `ctest --preset <preset>`                  |
| Integration   | `test/integration`               | `scripts/test_server.sh <preset> <config>` |
| Stress        | `test/stress`                    | `scripts/stress_server.sh <preset> <config>` |

The stress test (`redis_stress_test`) is not a benchmark. Many clients work on
their own keys against a local model, so every reply has an exact expected
value, with pipelining, large binary values, requests split across reads, and
malformed input; shared counters and sets are checked for exact totals. hiredis
only encodes and decodes, so a framing bug cannot hide on both sides. A failure
names the client, the request number and the first wrong byte, with that
client's last exchanges, and prints the seed to replay it. Use `SERVER_ARGS` and
`STRESS_ARGS` to run it against several I/O threads.

Tests are written BDD-style (`SCENARIO` / `GIVEN` / `WHEN` / `THEN`). Files
are named `<component>.test.cpp`; helpers shared by sibling suites live in a
`<component>.fixture.hpp` next to them. Scenarios tagged `[fork]` need a real
`fork()`.
