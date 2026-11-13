# User and Installation Guide

**Product:** Redis-compatible in-memory data store (`redis_server`)
**Document ID:** UG-001
**Structure:** ISO/IEC/IEEE 26514:2022 [1] (information for users)

## Document control

| Date        | Revision | Author             | Change        |
|-------------|----------|--------------------|---------------|
| 13 Nov 2026 | 1.0      | Khac Truong Nguyen | First version |

## Table of contents

1. [Introduction](#1-introduction)
2. [Installation](#2-installation)
3. [Getting started](#3-getting-started)
4. [Tasks](#4-tasks)
5. [Reference](#5-reference)
6. [Troubleshooting](#6-troubleshooting)
7. [Glossary](#7-glossary)
8. [References](#8-references)

---

## 1. Introduction

### 1.1 Purpose

`redis_server` is an in-memory key-value server that speaks the Redis protocol. This guide tells you how to build it,
run it, connect to it, keep its data safe and find out what it is doing.

### 1.2 Audience

* **Application developers** who connect with a Redis client library (section 4.1, section 5.3).
* **Operators** who run the server (sections 2, 4.2 to 4.7, section 5.1, section 6).

You need to be comfortable with a command line. You do not need to know C++ to use the server.

### 1.3 What it is and is not

It is compatible with Redis clients such as `redis-cli`, `redis-benchmark` and hiredis, for the commands in section
5.3. It is **not** a complete Redis. Not available: sorted sets, streams, Pub/Sub, transactions (`MULTI`/`EXEC`), Lua
scripting, replication, clustering, multiple databases (`SELECT`), authentication (`AUTH`) and a memory limit with
eviction. Its snapshot file is its own format; Redis tools cannot read it.

### 1.4 Conventions

| Convention      | Meaning                                                              |
|-----------------|----------------------------------------------------------------------|
| `monospace`     | Commands, options, file names and replies                            |
| `<name>`        | A value you supply                                                   |
| `[ ... ]`       | Optional                                                             |
| `$`             | A shell prompt; do not type it                                       |
| Command names   | Case-insensitive: `set` and `SET` are the same                       |

> **Warning:** the server has no authentication. Anyone who can reach its port can read, change and delete all data.
> Keep it on `127.0.0.1` or on a private network.

## 2. Installation

### 2.1 Supported platforms

| Platform                          | Compiler                         | Status                                                                 |
|-----------------------------------|----------------------------------|------------------------------------------------------------------------|
| Linux                             | GCC 14 or later, or Clang 18 or later | Supported                                                         |
| macOS (Apple Silicon)             | Homebrew LLVM                    | Supported                                                              |
| Windows                           | Clang-cl, MinGW                  | Builds for development; `BGSAVE`, `BGREWRITEAOF` and the crash report are unavailable |

The README lists the compilers CI uses. The code uses GNU statement expressions, so the compiler must be GCC or Clang.

### 2.2 Requirements

* CMake 3.28 or later, and Ninja
* [vcpkg](https://learn.microsoft.com/vcpkg/get_started/get-started), with the environment variable `VCPKG_ROOT`
  pointing at its checkout
* On macOS: `brew install llvm ninja`
* Optional: `redis-cli` and `redis-benchmark` (the `redis-tools` package on Debian and Ubuntu) to talk to and measure
  the server

Dependencies (Boost.Asio, spdlog, fmt, CLI11, abseil, reflect-cpp, stdexec, and for tests Catch2 and hiredis) are
installed by vcpkg during configuration. The first configuration builds them and takes a while.

### 2.3 Build

```sh
$ git clone https://github.com/sleepyviper1213/redis.git
$ cd redis
$ cmake --workflow --preset linux-gcc-debug     # configure, build, run the unit tests
$ cmake --build --preset linux-gcc-release --target redis_server
```

Use the `macos-arm64-llvm` prefix on macOS and `linux-clang` for Clang. `cmake --list-presets=all` lists everything.
The server binary is in the build tree for the preset; `scripts/run.sh` finds it for you.

### 2.4 Check the installation

```sh
$ scripts/run.sh linux-gcc Release -- --port 6379
$ redis-cli -p 6379 PING
PONG
```

If you see `PONG`, the server works. Stop it with Ctrl+C.

## 3. Getting started

### 3.1 Start the server

```sh
$ redis_server                          # listens on 127.0.0.1:6379
$ redis_server --port 6380 --io-threads 4
$ redis_server --help                   # every option
$ redis_server --version
```

At start the server loads its data (section 4.3), logs `Ready to accept connections`, and accepts clients.

### 3.2 First session

```console
$ redis-cli -p 6379
127.0.0.1:6379> SET greeting hello
OK
127.0.0.1:6379> GET greeting
"hello"
127.0.0.1:6379> SET session abc EX 60
OK
127.0.0.1:6379> TTL session
(integer) 60
127.0.0.1:6379> RPUSH jobs a b c
(integer) 3
127.0.0.1:6379> LRANGE jobs 0 -1
1) "a"
2) "b"
3) "c"
127.0.0.1:6379> DEL greeting jobs
(integer) 2
```

### 3.3 Stop the server

Press Ctrl+C, or send `SIGTERM` (`kill <pid>`). The server finishes the work in progress, saves a snapshot and exits
with status 0 (section 4.7).

## 4. Tasks

### 4.1 Connect from an application

Use any Redis client library, with host `127.0.0.1` and port `6379`. The server speaks RESP2; set your client to
protocol version 2 if it offers a choice. You can also test by hand with `telnet` or `nc`: type a command and press
Enter (inline commands, section 5.4).

Commands from one client are answered in order. To send many commands at once, use your client's pipelining; the
server answers a whole pipeline with one write.

### 4.2 Configure the server

Each option can be given three ways. Set each option in one place only: this guide does not specify which source wins
when the same option appears in more than one.

```sh
$ redis_server --port 6380 --appendonly                  # command line
$ REDIS_PORT=6380 REDIS_APPENDONLY=1 redis_server        # environment
$ redis_server --config server.toml                      # config file
```

Config file (TOML), using the long option names without the leading `--`:

```toml
port = 6380
log-level = "warning"
```

The full list of options is in section 5.1.

### 4.3 Keep data across restarts

There are two mechanisms. You can use either or both.

| Mechanism      | How                         | After a crash you lose                                  | Cost                              |
|----------------|-----------------------------|---------------------------------------------------------|-----------------------------------|
| Snapshot       | Always on                   | Everything since the last snapshot                      | A pause for `SAVE`; none for `BGSAVE` |
| Append-only file | `--appendonly`            | At most one second of writes (`everysec`), or none (`always`) | One log write per write command |

**Snapshots.** `SAVE` writes the whole keyspace to the `--dump` file and blocks all clients until done. `BGSAVE` does
the same in a background process and replies at once with `Background saving started`; `LASTSAVE` tells you when the
last snapshot finished. A snapshot is also written at shutdown. The file is replaced atomically, so a crash during a
save leaves the previous snapshot intact.

**Append-only file.** With `--appendonly`, every write command is logged to the `--appenddir` directory and replayed
at startup. When the append-only file is on, it is the source of truth at startup, not the snapshot. The fsync
policy `--appendfsync` chooses when the log is forced to disk: `always` (before replying; safest, slowest),
`everysec` (once a second; the default) or `no` (the operating system decides).

Only one server may use an append-only directory; a second one refuses to start.

**Compacting the log.** The log grows with every write. `BGREWRITEAOF` writes a fresh compact base in the background
and then removes the old files. Check progress in `INFO persistence` (`aof_rewrite_in_progress`).

### 4.4 Expire keys

```console
127.0.0.1:6379> SET token abc EX 300      # expires in 300 seconds
127.0.0.1:6379> EXPIRE token 60           # change the deadline
127.0.0.1:6379> TTL token                 # seconds left; -1 no deadline, -2 no such key
127.0.0.1:6379> PERSIST token             # remove the deadline
```

An expired key is never returned. It is removed when accessed and by a sweep every 100 ms that removes up to 1000
keys, oldest first, so a key may use memory for a moment after its deadline.

### 4.5 Watch the server

* **Statistics:** `INFO` (add `all` for per-command timings). Fields to watch: `connected_clients`,
  `rejected_connections`, `used_memory_human`, `instantaneous_ops_per_sec`, `expired_keys`, `keyspace_hits` and
  `keyspace_misses`, `rdb_last_save_time`.
* **Logs:** stderr and the `--log-file` (default `redis_server.log`), one line per message with the time, the
  channel (`server`, `net`, `commands` or `persistence`) and the level.
* **Slow commands:** while one command runs longer than `--watchdog-ms` (default 1000), the log shows
  `Command '<name>' (<n> args) has been running for <ms> ms; no other command runs until it ends`. A typical cause is
  `KEYS` on a large keyspace.
* **Crash report (Linux, macOS):** on a fatal signal the server writes the command that was running and a stack trace
  between `=== REDIS BUG REPORT START` and `END` lines, to stderr and the log. Include it in a bug report.

### 4.6 Change the log level while running

Start the server with `--enable-config-set`, then:

```console
127.0.0.1:6379> CONFIG SET loglevel debug
OK
127.0.0.1:6379> CONFIG GET loglevel
1) "loglevel"
2) "debug"
```

Levels: `trace`, `debug`, `info`, `warning`, `error`, `critical`, `off`. Because there is no authentication, enable this
only on a private port. The change is lost at restart; use `--log-level` to make it permanent.

### 4.7 Stop the server safely

Use Ctrl+C or `SIGTERM`. Avoid `SIGKILL`, which skips the final snapshot. If the final snapshot cannot be written, the
server logs `final snapshot failed` and exits with a non-zero status, so a service manager notices.

### 4.8 Limit misbehaving clients

| Option                          | Effect                                                                                  |
|---------------------------------|-----------------------------------------------------------------------------------------|
| `--maxclients <n>`              | A client over the limit gets `ERR max number of clients reached` and is closed          |
| `--idle-timeout <s>`            | Closes a client that sends nothing for that long                                        |
| `--request-timeout <s>`         | Closes a client whose request is not answered in time                                   |
| `--client-query-buffer-limit`   | Closes a client with too much unparsed input                                            |
| `--client-output-buffer-limit`  | Closes a client with too many unsent reply bytes                                        |

Closures are counted in `INFO`. A client that is closed by a limit or a lost connection cannot tell whether its last
command ran. Retry only commands that are safe to repeat (`SET`, `DEL`); retrying `INCR` or `LPUSH` blindly may apply
it twice.

### 4.9 Measure performance

```sh
$ redis-benchmark -p 6379 -q -P 16 -t set,get,incr,lpush,rpush,lpop,rpop,sadd,hset,spop,mset
```

For a controlled comparison with Redis, use `scripts/bench_server.sh`; see the benchmark report [2].

## 5. Reference

### 5.1 Options

| Option                          | Default            | Environment variable                | Meaning                                                    |
|---------------------------------|--------------------|-------------------------------------|------------------------------------------------------------|
| `--help`, `--version`           |                    |                                     | Print help or the version and exit                         |
| `--config <file>`               |                    | `REDIS_CONFIG_FILE`                 | Config file (must exist)                                   |
| `--host <ipv4>`                 | `127.0.0.1`        | `REDIS_HOST`                        | Address to listen on (IPv4 only)                           |
| `-p`, `--port <port>`           | `6379`             | `REDIS_PORT`                        | Port to listen on                                          |
| `--io-threads <n>`              | `1`                | `REDIS_IO_THREADS`                  | 1 to 256. Above 1, a separate thread executes commands     |
| `--dump <file>`                 | `dump.snap`        | `REDIS_DUMP_FILE`                   | Snapshot file                                              |
| `--appendonly`                  | off                | `REDIS_APPENDONLY`                  | Enable the append-only file                                |
| `--appendfsync <policy>`        | `everysec`         | `REDIS_APPENDFSYNC`                 | `always`, `everysec` or `no`                               |
| `--appenddir <dir>`             | `appendonlydir`    | `REDIS_APPENDDIR`                   | Directory of the append-only files                         |
| `--maxclients <n>`              | `10000`            | `REDIS_MAXCLIENTS`                  | Simultaneous clients                                       |
| `--client-query-buffer-limit <bytes>` | `1073741824` | `REDIS_CLIENT_QUERY_BUFFER_LIMIT`   | Unparsed bytes per client                                  |
| `--client-output-buffer-limit <bytes>` | `268435456` | `REDIS_CLIENT_OUTPUT_BUFFER_LIMIT`  | Unsent reply bytes per client                              |
| `--idle-timeout <s>`            | `0` (never)        | `REDIS_IDLE_TIMEOUT`                | Close idle clients                                         |
| `--request-timeout <s>`         | `0` (never)        | `REDIS_REQUEST_TIMEOUT`             | Close a client whose request is not answered in time       |
| `--enable-config-set`           | off                | `REDIS_ENABLE_CONFIG_SET`           | Allow `CONFIG SET loglevel` from any client                |
| `--watchdog-ms <ms>`            | `1000`             | `REDIS_WATCHDOG_MS`                 | Warn about a command running this long; 0 never            |
| `--log-file <file>`             | `redis_server.log` | `REDIS_LOGFILE`                     | Log file, in addition to stderr                            |
| `--log-level <level>`           | `info`             | `REDIS_LOGLEVEL`                    | `trace`, `debug`, `info`, `warning`, `error`, `critical`, `off` |

### 5.2 Files

| File                              | Purpose                                                                   |
|-----------------------------------|---------------------------------------------------------------------------|
| `dump.snap` (`--dump`)            | Snapshot; replaced atomically; own format, not Redis RDB                  |
| `appendonlydir/` (`--appenddir`)  | `appendonly.manifest`, `appendonly.<n>.base.snap`, `appendonly.<n>.incr.aof`, `appendonly.lock` |
| `redis_server.log` (`--log-file`) | Log and crash reports                                                     |

Do not edit the files by hand.

### 5.3 Commands

Counts are Redis' convention. Replies are Redis' too. "Wrong type" is
`WRONGTYPE Operation against a key holding the wrong kind of value`.

**Strings**

| Command | Syntax | Reply |
|---------|--------|-------|
| `SET` | `SET key value [NX \| XX] [GET] [EX s \| PX ms \| EXAT ts \| PXAT ms-ts \| KEEPTTL]` | `OK`; null if `NX`/`XX` not met; with `GET`, the old value |
| `GET` | `GET key` | The value; null if missing; wrong type |
| `MSET` | `MSET key value [key value ...]` | `OK` |
| `MGET` | `MGET key [key ...]` | Array of values; null for missing or non-string keys |
| `APPEND` | `APPEND key value` | New length |
| `STRLEN` | `STRLEN key` | Length; 0 if missing |
| `INCR`, `DECR` | `INCR key` | New value; `ERR value is not an integer or out of range`; overflow error |
| `INCRBY`, `DECRBY` | `INCRBY key n` | New value |

**Keys**

| Command | Syntax | Reply |
|---------|--------|-------|
| `DEL` | `DEL key [key ...]` | Number of keys removed |
| `EXISTS` | `EXISTS key [key ...]` | Number of keys that exist |
| `KEYS` | `KEYS pattern` | Array of matching keys, no defined order. Slow on large keyspaces |
| `TYPE` | `TYPE key` | `string`, `list`, `set`, `hash` or `none` |
| `EXPIRE`, `PEXPIRE` | `EXPIRE key seconds` | `1` if set, `0` if the key is missing |
| `EXPIREAT`, `PEXPIREAT` | `EXPIREAT key unix-time` | `1` or `0` |
| `TTL`, `PTTL` | `TTL key` | Time left; `-1` no deadline; `-2` no key |
| `PERSIST` | `PERSIST key` | `1` if a deadline was removed, else `0` |
| `DBSIZE` | `DBSIZE` | Number of keys |
| `FLUSHDB` | `FLUSHDB` | `OK`; deletes every key. Extra arguments are accepted and ignored |

Patterns: `*`, `?`, `[abc]`, `[^a]`, `[a-z]`, and `\` to escape.

**Lists**

| Command | Syntax | Reply |
|---------|--------|-------|
| `LPUSH`, `RPUSH` | `LPUSH key element [element ...]` | Length of the list |
| `LPOP`, `RPOP` | `LPOP key [count]` | The element, or an array with a count; null if missing |
| `LLEN` | `LLEN key` | Length; 0 if missing |
| `LRANGE` | `LRANGE key start stop` | Array; negative indexes count from the end |
| `LINDEX` | `LINDEX key index` | The element; null if out of range |

**Hashes**

| Command | Syntax | Reply |
|---------|--------|-------|
| `HSET` | `HSET key field value [field value ...]` | Number of new fields |
| `HGET` | `HGET key field` | The value; null if missing |
| `HMGET` | `HMGET key field [field ...]` | Array; null for missing fields |
| `HDEL` | `HDEL key field [field ...]` | Number removed |
| `HGETALL` | `HGETALL key` | Array of field, value, ...; no defined order |
| `HLEN`, `HEXISTS` | `HLEN key`, `HEXISTS key field` | Integer |

**Sets**

| Command | Syntax | Reply |
|---------|--------|-------|
| `SADD`, `SREM` | `SADD key member [member ...]` | Number added or removed |
| `SCARD` | `SCARD key` | Number of members |
| `SMEMBERS` | `SMEMBERS key` | Array; no defined order |
| `SISMEMBER` | `SISMEMBER key member` | `1` or `0` |
| `SINTER` | `SINTER key [key ...]` | Members common to all sets |
| `SPOP` | `SPOP key [count]` | One random member (removed), or an array with a count |

**Server**

| Command | Syntax | Reply |
|---------|--------|-------|
| `PING` | `PING [message]` | `PONG`, or the message |
| `ECHO` | `ECHO message` | The message |
| `SAVE` | `SAVE` | `OK` after the snapshot is written; blocks clients |
| `BGSAVE` | `BGSAVE` | `Background saving started`; Linux and macOS only |
| `LASTSAVE` | `LASTSAVE` | Unix time of the last snapshot |
| `BGREWRITEAOF` | `BGREWRITEAOF` | `Background append only file rewriting started`; needs `--appendonly`; Linux and macOS only |
| `INFO` | `INFO [section ...]` | Text. Sections: `server`, `clients`, `memory`, `stats`, `persistence`, `keyspace`; `commandstats` with `all` |
| `CONFIG GET` | `CONFIG GET pattern` | Name and value pairs: `appendonly`, `appendfsync`, `dbfilename`, `dir`, `loglevel`, `save` |
| `CONFIG SET` | `CONFIG SET loglevel level` | `OK`; needs `--enable-config-set` |

`QUIT`, `SELECT`, `AUTH`, `SCAN` and other commands not listed are unknown to this server and get
`ERR unknown command`. `redis-cli` closes the connection itself when you type `quit`.

### 5.4 Protocol

RESP2 over TCP. A request is an array of bulk strings; an inline line such as `PING` followed by Enter also works.
Values may contain any bytes when sent as bulk strings; in an inline line, quote values that contain spaces.

### 5.5 Error messages

| Message                                                          | Meaning                                              |
|------------------------------------------------------------------|------------------------------------------------------|
| `ERR unknown command '<name>', with args beginning with: ...`    | The command does not exist                           |
| `ERR wrong number of arguments for '<command>' command`          | Too few or too many arguments                        |
| `WRONGTYPE Operation against a key holding the wrong kind of value` | The key holds another type                        |
| `ERR value is not an integer or out of range`                    | A number was expected                                |
| `ERR syntax error`                                               | A bad option or combination                          |
| `ERR Protocol error: ...`                                        | Malformed request; the connection is then closed     |
| `ERR max number of clients reached`                              | `--maxclients` is reached                            |
| `ERR Background save already in progress`                        | A `BGSAVE` child is running                          |
| `ERR Append only file is not enabled`                            | `BGREWRITEAOF` without `--appendonly`                |
| `ERR CONFIG SET is disabled; start the server with --enable-config-set to allow it` | Section 4.6                 |
| `ERR Can't fork: function not supported`                         | `BGSAVE` or rewrite on Windows                       |

## 6. Troubleshooting

| Symptom | Likely cause | What to do |
|---------|--------------|------------|
| The server exits at once; the log says a file "is corrupt", "is not a snapshot file" or "has format version" | The snapshot or append-only file cannot be read | The server refuses to start rather than serve an empty keyspace. Restore the file from a backup, or move it away to start empty |
| The server exits with "is in use by another server" | Another server uses the same `--appenddir` | Stop the other server, or give this one its own directory |
| The server exits at start with an address error | The port is in use, or `--host` is not an IPv4 address | Use another `--port`, or find what holds it |
| `Connection refused` | The server is not running, or listens on another address or port | Check the log; check `--host` and `--port` |
| Connection from another machine fails | The default `--host` is `127.0.0.1` | Use a private address, never a public one: there is no authentication |
| Clients stall; the log shows "has been running for ... ms" | A slow command, such as `KEYS` or `SAVE` on a large keyspace | Avoid the command, use `BGSAVE`, or raise `--watchdog-ms` |
| `ERR max number of clients reached` | `--maxclients` reached | Close idle connections, set `--idle-timeout`, or raise the limit |
| `ERR Can't fork` | Windows, or the system is out of memory | On Linux and macOS check free memory; on Windows use `SAVE` |
| `ERR CONFIG SET is disabled` | Started without `--enable-config-set` | Restart with it, on a private port |
| Data missing after a crash | Only snapshots are on | Use `--appendonly` |
| The log file grows | `--log-level` is `trace` or `debug` | Use `info` or higher |
| The append-only directory grows | The log is never compacted | Run `BGREWRITEAOF` |

If the server crashes, keep the crash report from the log (section 4.5) and the command line, and open an issue in the
repository.

## 7. Glossary

| Term                | Meaning                                                                                       |
|---------------------|-----------------------------------------------------------------------------------------------|
| Append-only file    | A log of write commands, replayed at startup                                                  |
| Bulk string         | RESP's length-prefixed binary-safe string                                                     |
| Inline command      | A request typed as one line of words, as in telnet                                            |
| Keyspace            | All the keys and values the server holds                                                      |
| Pipelining          | Sending several requests before reading the replies                                           |
| RESP                | Redis Serialization Protocol                                                                  |
| Snapshot            | The whole keyspace written to one file at a point in time                                     |
| TTL                 | Time to live: seconds before a key expires                                                    |

## 8. References

[1] *Systems and software engineering—Design and development of information for users*, ISO/IEC/IEEE Std 26514:2022,
2022.

[2] K. T. Nguyen, "Benchmark and Evaluation Report," `docs/evaluation/benchmark_report.md`, 2026.

[3] Redis Ltd., "Redis commands," https://redis.io/docs/latest/commands/.

[4] Redis Ltd., "Redis serialization protocol specification,"
https://redis.io/docs/latest/develop/reference/protocol-spec/.
