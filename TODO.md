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
  cancelled session must unwatch on the keyspace thread. In the v2 chain that
  must run on value, error *and* stopped; `let_value` is skipped on
  `set_stopped`, so use `exec::finally` (or equivalent).

## Cancellation and timeouts

- [ ] **Idle and per-request timeouts in v2.** Race the read against a
  `steady_timer` (`async_wait` through `use_sender`) with `exec::when_any`, so
  whichever finishes first cancels the other through the stop token.
- [ ] **Document the delivery guarantee.** A command is applied entirely or not
  at all (cancellation is only honoured between operations), but a client
  cannot know which when the connection drops before the reply.
- [ ] **Log which stage failed in v2.** v1 logged "Read:" / "Write:"; v2 logs a
  generic "Connection:" because the error source is lost in the chain.

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

- [ ] **Same-key linearizability test.** Many clients racing `SET` / `DEL` /
  `GET` on one key; check every client's observed history is consistent with
  some single order (and that each client's own requests stay in order).
- [x] **Verifying stress test** (`test/stress`, `scripts/stress_server.sh`):
  private key partitions checked against a local model, strict RESP validation
  through `hiredis`, shared counter/set totals, random fragmentation, malformed
  and garbage input, a stall detector, and a seed plus per-connection trace so
  a failure replays and points at the exact request and byte. Checked against a
  fault-injecting proxy (flipped, duplicated and dropped replies are all caught).
  Still open: run it on Linux presets and under ThreadSanitizer (below), and
  add commands with array replies (`MGET`, `SMEMBERS`, `LRANGE`), which the
  reply reader in the test does not yet handle.
- [ ] **Differential test against real Redis** (Linux): same deterministic
  command stream to both, compare reply bytes, report the first divergence.
- [ ] **Run the multi-threaded mode under ThreadSanitizer** (`--io-threads N`,
  both session versions) on a Linux preset; it could not be run on mingw.
- [ ] **Audit `BGSAVE` forking from a multithreaded process.** The child must
  only touch memory it owns and then `_exit`; check logging, allocator and any
  locks held by the I/O threads at fork time.
- [ ] **Run the integration suite against v2 and several I/O threads:**
  `SERVER_ARGS="--sender-sessions --io-threads 4" scripts/test_server.sh`.

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
- [ ] **Accept backoff.** On a transient accept failure such as `EMFILE` both
  accept loops log and retry at once, which would spin and flood the log under
  sustained descriptor exhaustion. Back off briefly after an error.
- [ ] **`maxclients` limit** with a clear error to the client, so descriptor
  exhaustion is refused deliberately instead of failing at `accept`.
- [ ] **Scripting (Lua `EVAL`): decide whether to build it.** It is the other
  way Redis gives atomic multi-step operations, but it means embedding an
  interpreter and bounding script run time. `MULTI`/`WATCH` and atomic
  commands may be enough.
- [ ] **Slow-command visibility.** Log or count commands that hold the executor
  longer than a threshold (like Redis' `SLOWLOG`), so head-of-line blocking can
  be found.

## Sender-based networking (v2)

- [ ] **Benchmark with `redis-benchmark`:** v1 vs v2, and `--io-threads 1` vs N.
  The extra hop adds latency per batch; the gain should only show with many
  connections.
- [ ] **Unit test for `v2::TcpServer` shutdown** (stop, then `drained()`, with
  live clients). Needs a way to learn the bound port or an ephemeral-port
  option so the test does not depend on a fixed port.
- [ ] **Move cron and signal handling to senders** (`steady_timer` and
  `signal_set` through `use_sender`), so the whole v2 path is sender-based.
- [ ] **Decide the future of v1.** Neither version is deprecated yet.

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
