# ADR-0014: Drop privileges on Linux: Landlock at startup, seccomp before serving

- **Status:** Proposed
- **Date:** 2026-11-27
- **Requirements:** NFR-002
- **Design choice:** DX-95; scenarios TC-334 to TC-337

## Context

The server parses bytes from any client that can reach its port, and there is no authentication (NFR-002). A memory
bug in the parser or a command would give the attacker everything the process can do: open any file the user can,
start programs, open new connections. The fuzzers (DX-86) make such a bug less likely; they cannot rule it out.

Once the server has loaded its data and bound its port, it needs very little: network I/O on sockets it already
listens on or accepts, files in the dump and append-only directories and its log, memory, threads, `fork` for
`BGSAVE` and `BGREWRITEAOF`, and the clock. Linux can enforce that from inside the process, without root:
**Landlock** limits which paths can be opened and how, and **seccomp** limits which system calls can be made at all.
SerenityOS does the same with `pledge` and `unveil`.

Two facts decide where each goes:

* A seccomp filter installed with `SECCOMP_FILTER_FLAG_TSYNC` applies to every thread of the process, so it can
  wait until everything is set up: after the load and the bind, just before serving.
* A Landlock ruleset applies to the calling thread and the threads it creates afterwards, not to threads that
  already exist. The keyspace thread, which runs every command, starts with the server, so Landlock must be applied
  in `main()` before any thread is started (the log writer included).

## Decision

On Linux the server sandboxes itself by default; `--sandbox no` turns it off.

1. **Landlock, first thing in `main()`** after the options are parsed: read and write beneath the dump file's
   directory, the append-only directory (created first if missing) and the log file's directory; read only
   `/proc/self` (the crash report's symbols) and the time zone files. Everything else is refused with `EACCES`, which
   the server reports as it reports any file error. Best effort: on a kernel without Landlock it logs a warning and
   carries on with seccomp alone, as the Landlock documentation recommends.
2. **seccomp, in `RedisServer::run()`** after the load and `Listener::create`, before serving: an allow-list of
   system calls, installed with `TSYNC` and `PR_SET_NO_NEW_PRIVS`. A call outside it is not run: it raises `SIGSYS`
   (`SECCOMP_RET_TRAP`), which the crash handler reports, naming the call, before the process aborts. A call from
   another architecture's ABI (32-bit on x86-64) kills the process outright.
3. Builds with a sanitizer leave the sandbox off: the sanitizer runtimes make calls of their own (symbolizers,
   shadow memory) that the allow-list should not have to know. The debug and release builds in CI run every suite
   with it on.

`fork` children inherit both, so `BGSAVE` and the AOF rewrite run under the same rules.

## Alternatives considered

- **Off by default, opt in:** safer to roll out, but a protection nobody turns on protects nothing. CI runs every
  suite with the default, so a missing call is found there, and `--sandbox no` is the operator's way out.
- **Report only (`SECCOMP_RET_LOG`):** discovers the allow-list without risk but enforces nothing.
- **`SECCOMP_RET_KILL_PROCESS` for every violation:** the process dies with no report saying which call it was.
  `SECCOMP_RET_TRAP` costs nothing in safety (the call still does not run) and names the call.
- **libseccomp:** a dependency for what is about sixty lines of BPF over a fixed list.
- **Landlock in `RedisServer::run()`, with the other rules:** too late; it would not cover the keyspace thread.

## Consequences

- A bug that lets a client run code can no longer read other files, start a program, or open a connection.
- Every new system call the server makes (a new library, a libc upgrade, a new command) must be added to the
  allow-list, or the server stops with a report naming it. The integration suites are the check.
- `CONFIG SET dir` can only move saves within directories allowed at startup; elsewhere a save fails with a
  permission error. Redis 7 protects `dir` from `CONFIG SET` by default for the same reason.
- Other platforms are unchanged: the sandbox is a no-op there, logged once at startup.
