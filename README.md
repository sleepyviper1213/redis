# redis

[![CI](https://github.com/sleepyviper1213/redis/actions/workflows/ci.yml/badge.svg)](https://github.com/sleepyviper1213/redis/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
![C++23](https://img.shields.io/badge/C%2B%2B-23-informational)

A Redis-compatible in-memory key-value server written from scratch in C++23.
It speaks the Redis wire protocol, so `redis-cli`, `redis-benchmark` and
client libraries such as hiredis work against it unchanged.

```console
$ redis-cli -p 6379 SET greeting hello EX 60
OK
$ redis-cli -p 6379 TTL greeting
(integer) 60
$ redis-cli -p 6379 BGSAVE
Background saving started
```

## Features

- **RESP2 over TCP**, including pipelining (many commands per round trip),
  commands split across TCP reads, and inline commands (`PING\r\n`) as sent by
  telnet and `redis-benchmark`.
- **Typed keyspace**: strings, lists, sets and hashes, with `WRONGTYPE` errors
  when a command meets the wrong type.
- **Expiry like Redis**: lazily on access, plus a periodic cycle that removes
  due keys oldest-deadline-first from an ordered index, bounded per tick.
- **Snapshot persistence**: `SAVE`, and `BGSAVE` that `fork()`s so the child
  writes a copy-on-write image while the parent keeps serving. Snapshots are
  loaded at startup and written on SIGINT/SIGTERM.
- **Append-only file (AOF)**: enabled with `--appendonly`. Every write command
  is appended to an AOF and replayed at startup, so writes since the last
  snapshot survive a crash. Fsync policy is configurable: `always`,
  `everysec` (default), or `no`.
- **Crash-safe writes**: temp file, `fsync` (`F_FULLFSYNC` on macOS, where
  plain `fsync` stops at the drive's cache), atomic `rename`, then `fsync` of
  the directory. A reader sees the old snapshot or the new one, never half.
- **Crash reporter**: on POSIX, a signal handler writes the in-flight command
  and a stack trace to the log file before the process exits, so post-mortem
  debugging has context even without a core dump.
- **Defensive input handling**: client-declared lengths are bounded, per-client
  buffers are capped, and malformed input gets a RESP error before the
  connection closes.

### Supported commands

| Family  | Commands |
|---------|----------|
| Strings | `SET` (`EX` `PX` `EXAT` `PXAT` `KEEPTTL` `NX` `XX` `GET`), `GET`, `INCR`, `DECR`, `INCRBY`, `DECRBY`, `MSET`, `MGET`, `APPEND`, `STRLEN` |
| Keys    | `DEL`, `EXISTS`, `KEYS`, `TYPE`, `EXPIRE`, `PEXPIRE`, `EXPIREAT`, `PEXPIREAT`, `PERSIST`, `TTL`, `PTTL`, `DBSIZE`, `FLUSHDB` |
| Lists   | `LPUSH`, `RPUSH`, `LPOP`, `RPOP`, `LLEN`, `LRANGE`, `LINDEX` |
| Sets    | `SADD`, `SREM`, `SCARD`, `SMEMBERS`, `SISMEMBER`, `SINTER`, `SPOP` |
| Hashes  | `HSET`, `HGET`, `HMGET`, `HDEL`, `HGETALL`, `HLEN`, `HEXISTS` |
| Server  | `PING`, `ECHO`, `SAVE`, `BGSAVE`, `LASTSAVE`, `CONFIG GET`, `CONFIG SET` |

Not implemented: sorted sets, streams, pub/sub, transactions, replication and
multiple databases (`SELECT`). The snapshot format is this project's own, not
Redis' RDB.

## Performance

Measured with `scripts/bench_server.sh` against real Redis on Apple Silicon
(M-series, `RUNS=7 REQUESTS=500000`), best of 7 runs:

| Scenario | This server | Redis | |
|----------|------------:|------:|-|
| 50 clients, `SET` | 98,348 rps | 104,254 rps | 94 % |
| 1 client, serial | 35,577 rps | 35,251 rps | ≈ 1× |
| Pipelined (`-P 16`) | 1,377,411 rps | 1,422,475 rps | 97 % |
| Large-value `GET` | **1,060,445 rps** | 823,045 rps | **+29 %** |

The same benchmark on the Linux CI runners (4 CPUs, `RUNS=3 REQUESTS=200000`,
1 client thread, best of 3 runs). Shared runners are noisy, so read these as
rough ratios, not absolute numbers:

| Scenario | GCC | Redis | | Clang | Redis | |
|----------|----:|------:|-|------:|------:|-|
| 50 clients, `SET` | 115,340 rps | 120,700 rps | 96 % | 84,818 rps | 86,281 rps | 98 % |
| 1 client, serial | 30,883 rps | 29,958 rps | 103 % | 34,495 rps | 32,031 rps | 108 % |
| Pipelined (`-P 16`) | **1,724,138 rps** | 1,328,904 rps | **+30 %** | 1,307,190 rps | 1,351,351 rps | 97 % |
| Large-value `GET` | **1,089,918 rps** | 907,029 rps | **+20 %** | **1,307,190 rps** | 909,091 rps | **+44 %** |

The large-value `GET` lead comes from zero-copy argument parsing (args are
`string_view`s into the read buffer) and writing replies straight into the
output buffer without an intermediate copy. The keyspace is backed by
`absl::flat_hash_map` by default; `boost::unordered_flat_map` or
`std::unordered_map` can be selected at configure time with
`-DREDIS_KEYSPACE_MAP=boost|std`.

## Design

```text
client ──TCP──► net/session ──► resp::parse_request ──► commands/handler ──► storage/database
                     ▲                                        │
                     └──────── one write per batch ◄──────────┘
                                                 net/redis_server cron (100 ms):
                                                 active expiry, reap BGSAVE child
```

- **One command thread, no locks.** Like Redis, commands run one at a time on
  a single thread, so the keyspace needs no synchronisation. By default that
  thread also runs the event loop (Boost.Asio, stdexec senders). With
  `--io-threads N` (N > 1) reading, parsing and writing spread over N I/O
  threads, and parsed batches are handed through stdexec to one dedicated
  thread that executes them. Latency comes from keeping each command and each
  housekeeping step short and bounded.
- **Framing is separate from execution.** The session buffers bytes and cuts
  out complete requests; a frame split across reads waits for more bytes, and
  a batch of pipelined frames is answered with a single write.
- **Two expiry indexes.** A hash map answers `TTL` in O(1); a set ordered by
  deadline lets the periodic cycle remove due keys in O(log n) each, without
  the scanning or random sampling a single map would need.
- **Errors are values.** Parsing and persistence return `std::expected`;
  commands turn failures into RESP error replies. Nothing in the request path
  throws.
- **POSIX in one file.** `fork`, `waitpid` and `fsync` live in
  `persistence/posix.cpp`; everything else is portable C++.

The module layout and dependency rules are in
[docs/directory_layout.md](docs/directory_layout.md).

## Building

Requires Linux or macOS (on Windows, use WSL), CMake 3.28+ with Ninja, and
[vcpkg](https://learn.microsoft.com/vcpkg/get_started/get-started) with
`VCPKG_ROOT` pointing at its checkout. GNU statement expressions are used, so
the compiler must be GCC or Clang:

| Platform    | Compiler | Preset prefix |
|-------------|----------|---------------|
| Linux       | GCC 14+  | `linux-gcc` |
| Linux       | Clang 18+ | `linux-clang` |
| macOS (Apple Silicon) | Homebrew LLVM (`brew install llvm ninja`) | `macos-arm64-llvm` |

On macOS the presets use Homebrew's LLVM and libc++ rather than AppleClang,
whose standard library lags behind on C++23. Every vcpkg dependency is built
with the same toolchain (see
[cmake/toolchains/homebrew-llvm.cmake](cmake/toolchains/homebrew-llvm.cmake));
set `HOMEBREW_LLVM_PREFIX` if LLVM is not at `/opt/homebrew/opt/llvm`.

Dependencies (Boost.Asio, spdlog, fmt, CLI11, cereal, Catch2, hiredis, ...)
are declared in [vcpkg.json](vcpkg.json) and installed by the configure step.

```sh
git clone https://github.com/sleepyviper1213/redis.git && cd redis

# Configure, build and run the unit tests in one step
# (macOS: use the macos-arm64-llvm prefix instead of linux-gcc)
cmake --workflow --preset linux-gcc-debug

# Integration tests: start a server and drive it through hiredis
scripts/test_server.sh linux-gcc Debug

# Run the server
scripts/run.sh linux-gcc Release -- --port 6379 --dump ./dump.snap
```

`cmake --list-presets=all` shows every configuration. The ones CI runs:

| Preset                   | What it checks |
|--------------------------|----------------|
| `linux-gcc-debug`        | GCC build, unit + integration tests |
| `linux-gcc-release`      | Optimised build with LTO |
| `linux-clang-debug`      | Clang build |
| `linux-gcc-asan`         | AddressSanitizer + UndefinedBehaviorSanitizer |
| `linux-gcc-tsan`         | ThreadSanitizer |
| `macos-arm64-llvm-debug` | macOS build with Homebrew LLVM, unit + integration tests |
| `linux-clang-analysis`   | clang-tidy and cppcheck over every file |

### Server options

| Option | Default | |
|--------|---------|-|
| `--host`, `--port` | `127.0.0.1`, `6379` | Listen address (`REDIS_HOST`, `REDIS_PORT`) |
| `--io-threads` | `1` | Network I/O threads; above 1, commands run on a separate thread (`REDIS_IO_THREADS`) |
| `--dump` | `dump.snap` | Snapshot file (`REDIS_DUMP_FILE`) |
| `--timeout` | `0` (never) | Close a client that sends nothing for this many seconds (`REDIS_TIMEOUT`) |
| `--request-timeout` | `0` (never) | Close a client whose request is not executed and answered within this many seconds; not a Redis setting (`REDIS_REQUEST_TIMEOUT`) |
| `--appendonly` | off | Enable the append-only file; write every command to it and replay at startup (`REDIS_APPENDONLY`) |
| `--appendfsync` | `everysec` | AOF fsync policy: `always`, `everysec`, `no` (`REDIS_APPENDFSYNC`) |
| `--appenddir` | `appendonlydir` | Directory for AOF files (`REDIS_APPENDDIR`) |
| `--log-level` | `info` | `trace` … `critical`, or `off` (`REDIS_LOGLEVEL`) |
| `--log-file` | `redis_server.log` | Log file in addition to stderr (`REDIS_LOGFILE`) |
| `--config` | | Config file with any of the above (`REDIS_CONFIG_FILE`) |

## Testing

- **Unit tests** (`test/core`, `test/server`) run in-process with no sockets:
  the protocol parser, glob matching, the keyspace and its expiry indexes,
  every command family and snapshot round-trips, including corrupt and
  truncated files.
- **Integration tests** (`test/integration`) run against a live server
  through hiredis, the C client real applications use.
- Tests are written BDD-style (`SCENARIO` / `GIVEN` / `WHEN` / `THEN`) with
  Catch2 and run in random order to catch order-dependent tests.

## Benchmarking

Measured with the official `redis-benchmark` against this server and against
real Redis on the same machine, for an apples-to-apples comparison:

```sh
redis-benchmark -p 6379 -q -P 16 \
  -t set,get,incr,lpush,rpush,lpop,rpop,sadd,hset,spop,mset
```

## Documentation

- [Directory layout and request path](docs/directory_layout.md)
- [Software requirements specification](docs/analysis/software_requirement_specification.md)
- [Use case descriptions](docs/analysis/use_case_descriptions.md)
- [Design diagrams (PlantUML)](docs/analysis/diagrams/)
- [Test checklist](docs/test/TEST_CHECKLIST.md)

## License

MIT. See [LICENSE](LICENSE).

## Contact

Khac Truong Nguyen - [LinkedIn](https://www.linkedin.com/in/khactruongnguyen/) - truong_dev@icloud.com
