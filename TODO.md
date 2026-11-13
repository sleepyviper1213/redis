# TODO

Design notes and future work. Nothing here is implemented yet.

## Transactions

- [ ] **Per-client state in the command handler.** `CommandContext` is global
  today; `MULTI` needs a per-connection queue and a "dirty" flag, owned by the
  session and touched only on the keyspace thread while executing.
- [ ] **`MULTI` / `EXEC` / `DISCARD`.** Queue commands (`+QUEUED`), validate
  shape and arity at queue time (a bad one makes `EXEC` fail with `EXECABORT`),
  run the queue as one uninterrupted batch. Runtime errors such as `WRONGTYPE`
  do not roll back the other commands, as in Redis.
- [ ] **`WATCH` / `UNWATCH`.** Optimistic locking. Keyspace-side bookkeeping
  (key to watching clients, plus a dirty set) so a write by any session marks
  the watchers dirty; `EXEC` then returns nil.
- [ ] **Cleanup when a session ends, whatever the outcome.** A dropped or
  cancelled session must unwatch on the keyspace thread. In the session's
  sender chain that must run on value, error *and* stopped; `let_value` is skipped on
  `set_stopped`, so use `exec::finally` (or equivalent).

## Cancellation and timeouts

- [x] **Per-request timeouts.** `--request-timeout <seconds>`
  (`NetLimits::request_timeout`, zero for none): once a batch is read, it must
  be executed and its replies written within the limit, or the client is
  closed and `Violation::RequestTimeout` counted. The session races
  `respond()` (execute, then write) against a timer with `with_time_limit`,
  as the idle limit races the read. Two things the race needed: closing the
  socket when time runs out, because Windows does not complete a cancelled
  send the peer is not reading; and `unless_stop_requested` on the timer,
  because `use_sender` loses a stop requested before its wait starts, which
  made every round with nothing to answer wait out the whole limit.
- [x] **Document the delivery guarantee.** "What a client can rely on" in
  `docs/directory_layout.md`: a command is applied entirely or not at all; a
  batch stops part-way only at the output limit; a stop or a limit skips a
  queued batch, lets a running one finish and drops its reply; and a client
  cannot know which when its connection drops before the reply.
- [x] **Log which stage failed.** Each stage of the session handles its own
  error (`fail_io` with `ReadFailed`, `ExecuteFailed`, `WriteFailed`); only
  what escapes them is logged as a generic "Connection:".

## Retries and idempotency

- [ ] **Compare-and-set / compare-and-delete primitive** (or document that
  `WATCH` is the supported way), so clients can retry read-modify-write and
  release locks safely.
- [ ] **Rewrite layer for AOF / replication, if either is added.** Turn
  relative or non-deterministic commands into absolute, state-based ones before
  they leave the handler (`EX`/`PX` to absolute deadline, `INCRBYFLOAT` to the
  result, `SPOP` to `SREM`, ...). Snapshots already store absolute deadlines.
- [ ] **Blocking commands and delivery.** `BLPOP`-style pops are at-most-once
  if the connection drops after the pop; consider `BLMOVE`-style reliable
  queues when blocking commands are added.

## Concurrency verification

- [x] **Same-key linearizability test.** Phase 4 of the stress test: several
  clients (`--race-clients`, 8) pipeline `SET` / `DEL` / `GET` on one key in
  rounds (`--race-rounds`, 2000) separated by a barrier, so nothing is in
  flight between rounds. Each round is checked by search
  (`test/stress/linearizability.hpp`) from every value the last round may have
  left: some order must keep each client's own order and real time and explain
  every reply. Matching reads are placed greedily (provably without loss), so
  reads never branch: 200 rounds check in ~1.4 s on a Debug build. The checker
  has its own unit tests (`redis_stress_check_test`, run by CTest), so one that
  accepts everything cannot make a stress run pass. Passes on mingw with 1 and
  4 I/O threads.
- [x] **Verifying stress test** (`test/stress`, `scripts/stress_server.sh`):
  private key partitions checked against a local model, strict RESP validation
  through `hiredis`, shared counter/set totals, random fragmentation, malformed
  and garbage input, a stall detector, and a seed plus per-connection trace so
  a failure replays and points at the exact request and byte. Checked against a
  fault-injecting proxy (flipped, duplicated and dropped replies are all caught).
  Array replies are modelled too: `MGET`, `LPUSH`/`RPUSH`/`LPOP`/`RPOP`/
  `LRANGE`/`LLEN` on per-client lists, `SADD`/`SREM`/`SMEMBERS` (compared
  sorted)/`SISMEMBER`/`SCARD` on per-client sets, and the final sweep re-reads
  the lists and sets.
- [ ] **Differential test against real Redis** (`scripts/diff_test.py`, started
  by `scripts/diff_server.sh`): a seeded stream over a small keyspace (wrong
  types, missing keys, odd ranges, `SET` option combinations, bad arity,
  unknown commands) to both servers one request at a time, raw reply bytes
  compared (cut from the stream by hand, so `$-1` and `*-1` differ), unordered
  replies sorted. Reports the first divergence with the requests before it;
  `--keep-going` lists up to 20 and counts them per command, `--loose-errors`
  compares error codes only. Wired into CI's `linux-gcc-release` job against
  Debian's Redis, `continue-on-error` until its first findings are triaged
  (one is likely: `LPOP key count` on a missing key answers `$-1`, Redis
  `*-1`). Still open: run it, fix or accept each divergence, then make the
  step required.
- [ ] **Run the multi-threaded mode under ThreadSanitizer** (`--io-threads N`).
  CI's `linux-gcc-tsan` job runs the unit tests under TSan (passing), and now
  also the integration suite and the stress test against a TSan server with 1
  and 4 I/O threads. `scripts/common.sh`'s `stop_server` stops the server with
  SIGTERM and fails on an unclean exit or any sanitizer report on its stderr
  (which the scripts used to discard), so a race found while serving fails the
  step even when every reply was right. Still open: the first CI run.
- [x] **Audit `BGSAVE` forking from a multithreaded process.** The fork happens
  on the thread executing commands while the I/O threads, the async logger's
  worker and the AOF's fsync thread may be anywhere. What the child does:
  `save_snapshot()` and `close_inherited_descriptors()`, then `_exit`.
  - *Found and fixed:* the child closed the crash handler's log descriptor
    with the rest, so a crash in the child reported only to stderr, and the
    number could be reused by the snapshot file the child opens next, putting
    the report inside the snapshot. `os::keep_open_in_children()` now exempts
    it; tested in `unit/core/os/child_process.test.cpp`.
  - *Logging:* the child never logs (the snapshot code and `fork_and_run`
    report through the pipe instead), so a logger mutex held at the fork, or
    the async logger's queue, is never touched.
  - *Allocator:* the child allocates (paths, `fmt`, cereal's buffers,
    `opendir`). glibc's and macOS' malloc take their locks across `fork()`, and
    the keyspace's allocator (`common/store`) has no locks; a test forks while
    four threads allocate. A future custom allocator must be fork-safe too.
  - *Shared state:* the keyspace is read through `const Database &`; the
    debug-build thread-owner check passes because the child's only thread has
    the forking thread's id. Nothing the other threads own is read.
  - *Signals:* inherited handlers are harmless (Asio's signal pipe is closed
    in the child), and the parent stops it with SIGKILL.
  - *Residual risk:* if cereal throws in the child (a failed write), the
    unwinder takes the dynamic loader's lock through `dl_iterate_phdr`; had
    another thread held it at the fork, the child would hang and the save
    never finish. Unlikely (no thread loads libraries while serving), and not
    seen; a timeout on the child would bound it.
- [ ] **Run the integration suite with several I/O threads on Linux:** now a
  CI step on every Linux preset (`SERVER_ARGS="--io-threads 4"`). It passes on
  mingw with 1 and 4 I/O threads. Still open: the first CI run. (The script's
  port argument never reached the tests, which assumed 6380; it is now passed
  as `REDIS_TEST_PORT`.)

## Executor hand-off and fairness

Correctness comes from one consumer executing in FIFO order and replying only
after executing, not from batching or from the queue type. A request that
arrives after another client has *received* its reply is always executed after
it, which is the linearizability guarantee we need. The items below are about
cost and fairness, not correctness.

- [ ] **Measure the hand-off before replacing it.** Today's queue is the one
  inside `asio_thread_pool` (many I/O threads posting, one consumer). Profile
  with `redis-benchmark` at many connections; thread wake-ups are likely to cost
  more than the queue itself.
- [ ] **If the queue is the bottleneck:** try one SPSC ring per I/O thread with
  the executor polling them round-robin (no producer contention, fair), or a
  lock-free MPSC queue; and spin briefly before sleeping, as Redis' I/O threads
  do.
- [ ] **Bound the work per turn.** One batch holds the keyspace thread until it
  is done. The read chunk (16 KiB) caps it today; make the limit explicit
  (commands and/or bytes per batch) so a pipelining client cannot starve the
  others, and add a test for it.
- [ ] **Longer term: shard the keyspace across threads** (the Dragonfly
  approach) if one command thread is not enough. This needs per-key routing and
  a design for multi-key commands and transactions, so it is a different
  architecture, not a faster queue.

## Slow commands and resource limits

One command thread means one slow command stalls every client.

- [ ] **`SCAN` family** (`SCAN`, `HSCAN`, `SSCAN`) as the incremental
  alternative to `KEYS`, which is O(N) and blocks everyone on a big keyspace.
  Consider documenting `KEYS` as a debugging command.
- [ ] **Lazy freeing (`UNLINK`, async `FLUSHALL`).** Freeing a huge value on the
  executor thread blocks it. Unlink the value from the keyspace there, then hand
  the now-unreachable object to a worker thread to destroy. That is the only
  case where a second thread may touch data, and only because nothing else can
  reach it any more. (Check whether `UNLINK` exists yet.)
- [x] **Accept backoff.** After a failed accept (`EMFILE`, say) the accept loop
  logs and waits `ACCEPT_RETRY_DELAY` before trying again, so sustained
  descriptor exhaustion neither spins nor floods the log.
- [ ] **`maxclients` limit** with a clear error to the client, so descriptor
  exhaustion is refused deliberately instead of failing at `accept`.
- [ ] **Scripting (Lua `EVAL`): decide whether to build it.** It is the other
  way Redis gives atomic multi-step operations, but it means embedding an
  interpreter and bounding script run time. `MULTI`/`WATCH` and atomic
  commands may be enough.
- [ ] **Slow-command visibility.** Log or count commands that hold the executor
  longer than a threshold (like Redis' `SLOWLOG`), so head-of-line blocking can
  be found.
- [ ] **`MEMORY DOCTOR`.** As in Redis: a human-readable diagnosis of memory
  problems, with advice, rather than raw numbers (`INFO` has those). Candidates
  for what it reports: used memory against its peak, which needs the
  allocator's statistics; client input and output buffers near the
  `NetGuard` limits (`max_output_buffer`), and how often those limits were
  hit; the keyspace's size and its largest values. First check what memory
  accounting exists (`common/store` has an allocator) before designing the
  report. When there is nothing to report, say so in a sentence, as Redis
  does.

## Sender-based networking

- [x] **One version.** The coroutine sessions (v1) are gone; the sender-based
  sessions, once v2 behind `--sender-sessions`, are the only ones.
- [ ] **Benchmark with `redis-benchmark`:** before and after the merge and the
  coroutine accept loop, and `--io-threads 1` vs N. The hop to the keyspace
  thread adds latency per batch; the gain should only show with many
  connections. After the merge, against Redis 8.0.2 (CI `bench` workflow, gcc
  and clang, two runs of the same commit, one I/O thread): plain clients -0.2
  to -2.9%, one client +5 to +8%, pipelined +9 to +15%, `bigget` +16 to +21%.
  Two runs of identical code differed by up to ~6 points on these ratios, so
  read a smaller change between CI runs as noise. Still open: the comparison
  with the code before the merge, and `--io-threads N`.
- [ ] **Plain clients trail Redis slightly.** The one scenario where the server
  does not lead: behind in all four measurements above, by 0.2 to 2.9%. Small,
  but always on the same side. Profile the 50-client, unpipelined case
  (`scripts/profile_server_linux.sh <preset> RelWithDebInfo clients`).
- [x] **Unit tests for the listener** (`test/unit/server/net/listener.test.cpp`,
  on an ephemeral port): clients served, many at once, on 1 and 4 I/O threads;
  `stop()` while accepting and with a live client, twice, then refused
  connections; the loop ends without an error.
- [x] **Cron and signal handling as senders.** Housekeeping is a coroutine over
  a `steady_timer` through `use_sender`, and the signal wait is
  `signal_set::async_wait` through `use_sender`.
- [x] **Supervise the top-level tasks.** `RedisServer::run()` runs the accept
  loop, housekeeping and the signal wait under one `exec::when_any`: the first
  to finish cancels the others. `stdexec_ext::supervised` names each task's
  `FailPolicy`: the accept loop and housekeeping are `FailFast` (their failure
  ends the server with that error, where a dead accept loop used to leave it
  running), sessions are `Isolate`.
- [ ] **Test that a FailFast failure ends the server**, with the error returned
  from `run()` and the sessions drained. Accepts can now fail on demand
  (`BasicListener<FailingAcceptor>` in the listener test), but by design
  `retry` absorbs those, so the loop never fails from them. A FailFast end
  needs a failure outside `retry`: a seam in housekeeping (`cron()`), say.
- [ ] **Decide: a final snapshot after a FailFast failure?** `run()` returns
  the error without saving, so a dead accept loop loses the writes since the
  last save. Saving anyway would keep them whenever the keyspace thread itself
  is still healthy.
- [ ] **Fewer exceptions in the coroutines.** `serve()` catches the race's
  error with `try`/`catch` around the `co_await`, and the accept loop does the
  same around `retry`. Recording the error in an `upon_error` before the
  `co_await` throws nothing and keeps the reason (the handler records it).
- [x] **The accept loop as a coroutine** (`stdexec_ext::InlineTask`) that
  `co_await`s `retry` over one accept. The per-request round stays a sender
  chain: a task allocates a frame per call, and `co_await` turns `set_error`
  into an exception.
- [x] **Failed accepts, injected.** `BasicListener` takes its acceptor as a
  template parameter (`tcp::acceptor` in the server, so it costs nothing);
  the test's `FailingAcceptor` fails the next n accepts. Covered: the client is
  served once the failures stop, after `ACCEPT_RETRY_DELAY` each; a run of
  failures retries about once per delay, not in a tight loop; `stop()` while
  waiting after a failure ends the loop without an error.
- [ ] **Remaining accept-loop tests:**
  - `retry`: stop during an attempt, attempts that complete on another thread,
    and a policy that waits on a real timer;
  - no coroutine-frame allocation on the request path (count allocations in the
    session test).
- [x] **`InlineTask` is `stdexec::task` (P3552).** `stdexec::task<T,
  InlineTaskEnv>`, where the environment's `start_scheduler_type` is
  `inline_scheduler` (`core/stdexec_ext/task.hpp`): no scheduler needed to
  start, so `spawn()` and `start_detached()` take it, and no hop back after a
  `co_await`, so the accept loop and housekeeping resume where Asio completes
  them. No internal stdexec namespace any more. The second template argument
  is an environment, not a scheduler: `task<T, inline_scheduler>` would be
  an ordinary affine task, which `spawn()` rejects at compile time.
  `test/unit/core/stdexec_ext/task.test.cpp` checks the no-hop behaviour,
  which the compiler cannot.
- [ ] **Replace `asio_thread_pool` as the keyspace thread.** Nothing needs its
  Asio face any more: sessions and housekeeping only use `run_on()`'s
  scheduler. Try `exec::static_thread_pool(1)` or a single-thread context
  (`exec/single_thread_context.hpp`, if it is in the pinned stdexec). Before
  switching, check: tasks from all sessions run strictly in the order queued
  (add a test), `Executor::stop()` still drains (`sync_wait(schedule(...))`),
  and the debug-only worker-id probe still works. Benchmark with
  `redis-benchmark` before and after.
- [ ] **Make `stdexec_ext::retry`'s final step easier to read.** The
  `Failed` / `Done` / `Outcome` aliases exist because a `let_value` cannot
  return `just_error(e)` in one branch and `just(values...)` in the other.
  First try: give that step a named, commented helper (as `with_idle_limit`
  does) so the aliases sit in one place. Only then consider `exec::create`,
  which builds one sender that completes either way, but its callback must
  complete exactly once on every path and wire up stop by hand: a mistake is a
  hang at run time, where a wrong `variant_sender` alias fails to compile.
  Background: <https://github.com/NVIDIA/stdexec/discussions/1940> (no
  maintainer answer on which is idiomatic).

## Performance (SET)

- [ ] **Profile `SET` before optimising.** `redis-benchmark -t set -n 10000`
  measured 0.394 s against 0.294 s for Homebrew redis-server. Key hashing is
  nanoseconds per request, so the ~10 us per-request gap is more likely I/O,
  the executor hand-off, reply formatting or logging. Compare `-c 1` and
  `-P 16` to separate latency from CPU, and profile with `perf` or Instruments.
- [ ] **Compare keyspace maps.** Build one tree per
  `-DREDIS_KEYSPACE_MAP=boost|absl|std` and benchmark each. The flat maps
  invalidate pointers on insert; the commands were audited for that, but run
  the tests.
- [ ] **Redundant key hashing in `SET`.** A plain `SET` hashes the key about
  five times (`expires_` and `dict_` lookups in `find_as`, again in `set`, then
  `persist`). Skip `expires_` when empty and do a single `dict_` lookup.
- [ ] **Allocation-free command dispatch.** `toupper` allocates a string per
  request, plus an `unordered_map` lookup. Candidate: a `constexpr` perfect hash
  over the fixed command set (hash a few selected bytes and the length, search
  for a collision-free seed at compile time, check the first byte then compare).
  Use the same case-insensitive match for the `SET` options (`NX`, `EX`, ...).
  Avoid x86-only CRC32 intrinsics (the Mac is arm64). Reference, not to copy
  (CC-BY-NC-ND): <https://github.com/strager/perfect-hash-tables>.

## Tooling

- [ ] **AddressSanitizer on Windows with LLVM's clang-cl.** It works, but only
  by hand. `cmake/sanitizers/Address.cmake` looks for the runtime DLL beside the
  compiler, and LLVM keeps it in `lib/clang/<version>/lib/windows`. It also
  adds no runtime libraries, so `lld-link`, called directly by CMake, fails
  with undefined `__asan_*` symbols: with the static CRT it needs
  `clang_rt.asan_dynamic-x86_64.lib` and
  `clang_rt.asan_static_runtime_thunk-x86_64.lib`. And abseil's prebuilt
  library (no ASan) disagrees with ASan-built code about `raw_hash_set`'s
  layout (ASan turns on its generation tracking), which crashes inside the
  keyspace: build ASan trees with `-DREDIS_KEYSPACE_MAP=boost`, or abseil with
  ASan too.
- [ ] **Static analysis is mostly noise.** CI's clang-tidy reports about 900
  distinct warnings in our code, most from `misc-include-cleaner`, unchecked
  container access and magic numbers, and the job passes regardless, so a new
  real finding is easy to miss. Decide per check: fix it, or turn it off in
  `.clang-tidy`. cppcheck runs without `--enable`, so it reports only
  error-severity findings (both of today's are false positives).
- [x] **MinGW: one Asio thread type in every file.** Asio chose `posix_thread`
  or `win_thread` per file, by include order, so `win_iocp_io_context` had two
  layouts in one program: a crash, an invalid handle or a hang in
  `~io_context`. `BOOST_ASIO_HAS_PTHREADS` is now defined everywhere on MinGW
  (`cmake/Dependencies.cmake`). `test/support/crash_report_windows.cpp` makes
  such bugs report themselves: a crash prints its stack, and strict handle
  checking faults on the first invalid handle.

## Documentation and submission

What is left to do by hand for the graduation project. The documents are in
`docs/` (SRS, use cases, design, test, user guide, benchmark report); anomaly
IDs (A-n, B-n) refer to the test plan and the benchmark report.

- [ ] **Demo video.** Record it, upload it to YouTube, and replace `VIDEO_ID`
  (twice) in the "Demo video" section of `README.md`. Suggested order: build
  and start the server; `redis-cli` session (strings, lists, expiry); `INFO`;
  `BGSAVE`, kill the server, restart, data is back; `--appendonly` and
  `BGREWRITEAOF`; `redis-benchmark` against this server and Redis; stress test
  and CI. `BGSAVE` needs Linux or macOS: it does not work on Windows.
- [ ] **Run the levels that were not run for the test report** (integration,
  stress, differential; `--io-threads 1` and `4`) on Linux through CI, then
  fill in section 15.2 of `docs/test/test_plan_and_report.md` with the real
  results and tick the exit criteria in section 17.3.
- [ ] **Close the test anomalies.** A-5 first (integration `save` tests
  TC-001 to TC-006 carry the hidden tag `[.]`, so they never run: confirm,
  then run or fix them), then A-2 (option parsing and config precedence), A-6
  (`SIGTERM`: final save and exit status) and A-4 (re-enable `linux-gcc-asan`).
  Add A-1's fix (wider margins in the `watchdog` and `every` tests). Update the
  traceability table when a gap closes.
- [ ] **Rerun the benchmarks and republish the figures.** With the current
  `bench.yml` defaults, `RUNS=7`: report the median and the range next to the
  best (B-1, B-2), record the Redis version (B-6), and rerun the Clang
  pipelined and `bigget` rows with longer runs (B-10). Then update the tables
  in `README.md` and `docs/evaluation/benchmark_report.md` together.
- [ ] **Fix the README benchmark label (B-3).** The fourth row says
  "Large-value `GET`" but the scenario is `GET` over 2,000,000 random keys.
  Rename it, or add a real large-value scenario.
- [ ] **Check the option precedence** (command line, `REDIS_*` environment,
  `--config` file) by running the server, then state it in section 4.2 of
  `docs/user/user_guide.md` and in UC-016. The documents deliberately do not
  claim an order yet.
- [ ] **Update `docs/directory_layout.md`.** It still shows `server/storage`
  and `core/resp`; the code has `common/store` and `common/resp`.
- [ ] **Check the format against your school's template.** The documents are
  structured after IEEE 830 (SRS), 1016 (design), 829 (test, benchmark) and
  ISO/IEC/IEEE 26514 (user guide), not certified. Add what the template asks
  for that they lack: a title page, student and supervisor names, a
  declaration, an abstract, page numbers. If it wants one file, export with
  pandoc.
- [ ] **Render the diagrams** (`docs/analysis/diagrams/*.puml`), look at
  `use_case.puml` after the redraw, and decide whether the SRS should embed
  images (the SVGs were removed from the repository).
- [ ] **Review the written claims once.** The use cases, design and guide were
  written from the code and a few runs, not by running every command. Skim them
  against the server; `QUIT` is not supported, which the guide notes.
