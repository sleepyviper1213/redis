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
│   │   ├── error/         std::expected helpers (TRY, FAILED)
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
    └── integration/       hiredis tests against a running server
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

1. `net/tcp_server` accepts a connection and starts a `net/session` coroutine.
2. The session coroutine only does I/O: it appends incoming bytes to a buffer
   and hands it to `net/pipeline`'s `execute_pending`, a plain function that
   cuts out complete requests with `resp::parse_request` (RESP arrays from
   client libraries, or inline commands from telnet and redis-benchmark) and
   runs them. Keeping this hot loop out of the coroutine keeps its state off
   the heap-allocated coroutine frame. A request can be split across reads,
   and one read can carry several pipelined requests.
3. `commands/handler` checks the request shape and arity, then calls the
   command through a hash table built from each family's `CommandInfo` list.
4. The command reads or writes `storage/database` through typed lookups
   (`find_as<List>`, `find_or_create<Set>`), which return `WrongType` instead
   of a value of another type.
5. Replies to every request in the batch are RESP-encoded and sent back in one
   write.

## Background work

`net/redis_server` runs a 100 ms timer (Redis' `serverCron`) on the same event
loop. Each tick removes up to 1000 expired keys, oldest deadline first, and
collects a finished `BGSAVE` child with a non-blocking `waitpid`.

`BGSAVE` forks. The child serialises its copy-on-write view of the keyspace
and exits with `_exit`; the parent keeps serving. All POSIX calls live in
`persistence/posix.cpp`.

## Tests

| Kind          | Location                         | Runs with                                  |
|---------------|----------------------------------|--------------------------------------------|
| Unit          | `test/core`, `test/server`       | `ctest --preset <preset>`                  |
| Integration   | `test/integration`               | `scripts/test_server.sh <preset> <config>` |

Tests are written BDD-style (`SCENARIO` / `GIVEN` / `WHEN` / `THEN`). Files
are named `<component>.test.cpp`; helpers shared by sibling suites live in a
`<component>.fixture.hpp` next to them. Scenarios tagged `[fork]` need a real
`fork()`.
