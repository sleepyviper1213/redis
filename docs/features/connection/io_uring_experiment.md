# Experiment: is a request's cost in the syscalls, and would io_uring cut it? (TC-298)

Two stages, with a decision between them. Nothing in the server changes until stage 1 has been read.

## Question

Without pipelining, the server handles about 130,000 requests per second with 50 clients; with 16 commands per batch it
handles about 1,450,000 (Benchmark run #27, gcc job, a runner with 4 CPUs; the reference Redis there: 129,000 and
1,332,000). A per-request cost that pipelining removes is a cost of the round trip, not of the command: reading,
writing, waking the event loop. The guess is that it sits in system calls and context switches. This experiment checks
that guess before spending effort on it.

DPDK and `sendfile()` were considered and dropped: DPDK cannot run on the CI runners (no NIC to bind), and `sendfile()`
sends files, while replies come from memory.

## Stage 1: profile the non-pipelined case

**Method.** The Profile workflow (`.github/workflows/profile.yml`) runs `scripts/profile_server_linux.sh` with the
`clients` scenario: 50 clients, no pipelining, `SET`. RelWithDebInfo with frame pointers, `perf record` with call
graphs, and `perf stat` counters over the same window, once per compiler.

**What is read from the report**

| Figure | Where in the report | What it says |
|--------|--------------------|--------------|
| Task-clock per command (µs) | counters block | The server's CPU cost of one request |
| Context switches per 1000 commands | counters block | How often a request wakes a thread |
| Self time in kernel symbols (`tcp_sendmsg`, `tcp_recvmsg`, `ep_poll`, syscall entry, `sock_*`) against user symbols (session, parser, hash lookups, stdexec) | self-time list | Whether the CPU is spent in the kernel or in this code |
| Inclusive time of `Session` and `stdexec` frames | inclusive lists | Which stage of a request costs what |

**Threats to validity.** One run is one sample, and a shared runner is noisy: compare the lists inside one job, never
numbers across jobs. The 4 CPUs are shared with `redis-benchmark`, which takes some of them. A VM may not expose a
hardware PMU, in which case cycles, instructions and cache misses are missing (the script says so) and only the
software counters remain. Sampling at 999 Hz for 10 s gives on the order of ten thousand samples, enough for shares of a
few percent, not for fine differences.

**Decision.** Stage 2 is worth doing if the kernel's share of the server's CPU, system calls and the network stack
together, is about half or more, and the context-switch rate is of the order of one per request. If most of the time
is in this code (parsing, session, executor hops), io_uring would not help, and the finding to act on is there.
These thresholds are a judgement, set here before the numbers are seen.

## Stage 2: an io_uring build of the same server

Only if the decision above says so. The backend is a build option and changes no server code.

- **Mechanism.** In Boost.Asio 1.38.2 (Boost 1.92, the vcpkg version), `BOOST_ASIO_HAS_IO_URING_AS_DEFAULT` makes sockets
  use io_uring, and it is defined automatically only when io_uring is enabled and epoll is not. So the variant defines
  `BOOST_ASIO_HAS_IO_URING` and `BOOST_ASIO_DISABLE_EPOLL` and links `liburing`.
- **Option.** `REDIS_USE_IO_URING`, off by default, Linux only (an error elsewhere), a vcpkg feature `io_uring`
  (`liburing`), and a preset `x86_64-linux-gnu-gcc-iouring`: the pattern of `REDIS_USE_JEMALLOC` (ADR-0011).
- **Container permissions.** Docker's default seccomp profile may refuse `io_uring_setup`, and the test and benchmark
  jobs run in unprivileged containers (only the profile and ThreadSanitizer jobs are privileged). A probe step comes
  first, so a refusal shows up as a clear message, not as a server that cannot start.
- **Sanitizers.** Off in this variant at first, as for jemalloc.
- **Other platforms.** Windows and macOS builds do not change.

**What is measured.** The Benchmark workflow gains one job on the io_uring preset beside the existing ones: the
`clients`, `latency`, `pipeline` and `bigget` scenarios of `bench_server.sh`, with the reference Redis in the same job.
The full unit and integration suites run on the preset, as TC-275 does for jemalloc.

**Keep it, or drop it.** The option stays off by default whatever the result. It is recorded as worth having only if
the suite passes on it and the `clients` median is at least 10 % higher than the epoll build's in repeated runs
(at least three, the spread being larger than one run shows: the jemalloc memory runs differed by 4 to 7 % for
identical work), with no loss in `pipeline`. The numbers go in a results document in this folder, as for jemalloc.

## Results

### Stage 1 (Profile workflow run #13, commit `d0b37cc`, 2026-11-25)

`clients` scenario, 50 clients, no pipelining, 10 s sampled, a runner with 4 CPUs shared with `redis-benchmark`. The
server ran under `perf`, so its rate (about 91,000 and 112,000 per second) is below the unobserved 130,000.

| | gcc | clang |
|---|---:|---:|
| Commands handled in the window | 911,534 | 1,123,556 |
| Task-clock per command | 9.91 µs | 7.49 µs |
| Context switches per 1000 commands | 97.6 | 236.8 |
| CPU migrations per 1000 commands | 0.02 | 0.02 |
| Inside a system call (inclusive) | 77.4 % | 82.8 % |
| of which `sendto` | 53.5 % | 55.1 % |
| `recvfrom` | 7.1 % | 11.4 % |
| `epoll_wait` | 4.3 % | 8.7 % |
| `epoll_ctl` | 6.4 % | 3.0 % |

The hardware counters (cycles, instructions, cache misses) are not available: the runner's VM exposes no PMU.

**Reading it**

- **The kernel is where the time goes.** Three quarters or more of the server's CPU is inside system calls, and the
  code of this server (parsing, session, hash lookups, stdexec) is under a quarter. Making that code twice as fast
  would speed a request up by about 10 %.
- **Wakeups are not the cost.** Context switches are 0.1 to 0.24 per request, not about one: with 50 clients the loop
  is rarely idle, so one `epoll_wait` returns several ready sockets.
- **Most of it is `sendto`, and much of that is not this server's work.** About 54 % of the CPU is in `sendto`. The
  client is on the same machine, so over loopback the kernel runs the client's receive path inside the server's
  system call: `tcp_v4_rcv`, `tcp_rcv_established`, `tcp_ack` and `tcp_clean_rtx_queue` are in the server's self-time
  list. This is an inference from those symbols; it was not isolated. On a real network card that part would run
  elsewhere, so this profile overstates the server's own share, and io_uring cannot remove it either way: it does
  the same TCP work.
- **`epoll_ctl` appears at 3 to 6 %.** A socket registered once with the reactor needs no per-request `epoll_ctl`. That
  it shows up suggests a registration change per request, which would be a cost with or without io_uring. Not
  investigated.

**Against the decision rule.** The first half is met (the kernel's share is about 77 to 83 %, against "about half").
The second half is not (about 0.1 to 0.24 context switches per request, against "of the order of one"). What io_uring
could remove is the system-call entry and exit, `epoll_wait` and `epoll_ctl`, and some per-call overhead of `sendto`
and `recvfrom`: roughly 15 % of the server's CPU at most (self time of `do_syscall_64` plus the two `epoll`
calls, a rough bound). That is near the 10 % bar for keeping the option and does not leave room for it to miss.

### Stage 1b: where the `epoll_ctl` comes from

Read in Asio 1.38.2's source (`epoll_reactor.ipp`, `reactive_socket_service_base.hpp`, `reactive_socket_recv_op.hpp`);
not yet measured.

- **`async_wait(wait_read)` costs one `epoll_ctl` per call.** It starts its operation with speculation off, and the
  reactor then re-arms the socket's edge-triggered registration with `epoll_ctl(EPOLL_CTL_MOD)`
  (`epoll_reactor.ipp`, the `else` branch of `start_op`). `Session::read()` waits this way once per request, which is
  the 3 to 6 % in the profile.
- **`async_read_some` avoids it, but tries a `recv` first.** Its operation allows a speculative attempt. After a read
  that completed from the reactor, Asio stops speculating only if the read was *exhausted*; by default a stream read
  counts as exhausted only when it returned no bytes. A short read does not, so the next `async_read_some` calls `recv`
  first and finds nothing: the wasted syscall that the comment in `Session::read()` describes, and the reason it
  uses `async_wait`.
- **So the session chose between two one-syscall options.** Either an `epoll_ctl` per request or a failed `recv` per
  request.
- **Asio has a switch for a third.** The configuration key `reactor.reset_edge_on_partial_read=1` makes a stream read
  that filled less than its buffer count as exhausted. Speculation is then skipped after a short read, and
  `async_read_some` starts with no system call at all. It is set when the `io_context` is built, from a
  `config_from_string`; today `RedisServer` builds it from a concurrency hint, which would have to be given as
  `scheduler.concurrency_hint=N` in the same string.

**Variant to measure.** `Session::read()` waits with `async_read_some` into the connection's read buffer, and the
`io_context` is built with `reactor.reset_edge_on_partial_read=1`, on Linux (epoll) only: the other platforms keep
today's code, since the trade-off was measured on one of them and the epoll behaviour above does not apply to
kqueue or IOCP.

**Expected.** One system call fewer per request: `epoll_ctl` at about 3 to 6 % of the server's CPU should fall to
nearly nothing in the `clients` profile, and `clients` throughput rise by about as much. A larger gain is not
expected; a loss would mean the reset-edge setting costs more than it saves.

**Risks to rule out**
- A lost wakeup: a partial read treated as exhausted while data is already there would leave a request unanswered
  until the idle timeout. The stress test, which splits every request at every byte boundary and checks every reply,
  is the check; the unit and integration suites must also pass on Linux.
- The idle and request time limits cancel the pending read; cancelling an `async_read_some` must behave as cancelling
  the wait does. The session tests for both limits cover it.
- The single-client case (`latency`): the wasted `recv` was measured as a quarter of the time there. It must not come
  back.

**Measured by.** The Profile workflow's `clients` step (the `epoll_ctl` share and the task-clock per command, against
the figures above), and the Benchmark workflow's `clients` and `latency` scenarios, in a job that has both builds.

**Built, not yet measured.** `Session::receive()` reads with one `async_read_some` where Asio uses epoll, and
`RedisServer` builds its `io_context` from `io_config()` (`src/server/net/detail/io_config.hpp`), which keeps the
concurrency hint and adds the reactor setting. The other platforms keep the wait-then-read path. The unit suite
passes on Windows (IOCP), with the new read also forced on there to exercise the cancellation, end-of-stream and
timeout handling; the epoll reactor itself runs only in Linux CI (TC-299 for the configuration, the stress test for
lost wakeups, and the Profile and Benchmark workflows for the cost).

### Stage 1b results (Profile workflow run #14, commit `3d04455`, 2026-11-25)

The same `clients` step, now with `Session::receive()` reading in one `async_read_some`. Both jobs built and ran on
Linux, so the epoll branch compiles with gcc and clang.

| | gcc, before | gcc, after | clang, before | clang, after |
|---|---:|---:|---:|---:|
| Commands handled in 10 s, under `perf` | 911,534 | 1,869,109 | 1,123,556 | 1,925,591 |
| Task-clock per command | 9.91 µs | 5.04 µs | 7.49 µs | 4.84 µs |
| Context switches per 1000 commands | 97.6 | 22.8 | 236.8 | 25.1 |
| `epoll_ctl`, inclusive | 6.4 % | absent | 3.0 % | absent |
| `sendto`, inclusive | 53.5 % | 58.2 % | 55.1 % | 58.7 % |
| `recvfrom`, inclusive | 7.1 % | 11.5 % | 11.4 % | 11.2 % |
| `epoll_wait`, inclusive | 4.3 % | 5.3 % | 8.7 % | 5.1 % |
| Inside any system call | 77.4 % | 77.4 % | 82.8 % | 77.5 % |

**Reading it, with its limits**

- **`epoll_ctl` is gone,** as intended. That part is certain.
- **The effect is about ten times what the profile predicted.** The prediction was the 3 to 6 % that `epoll_ctl` took.
  CPU per command fell by about 49 % (gcc) and 35 % (clang), the rate under `perf` roughly doubled (gcc) and rose by
  about 70 % (clang), and context switches fell four to nine times. The server's thread was busy about nine tenths of
  the window in both runs (9.0 and 9.4 s of CPU in 10 s), so a lower cost per command is a higher rate.
- **The mechanism is not established.** The shares of the system calls barely moved while the cost per command nearly
  halved, so every part of a request got cheaper, not just the one removed. A guess, not tested: re-arming a
  registration with `epoll_ctl(EPOLL_CTL_MOD)` while data is already waiting puts the socket back on the ready list,
  which would explain more `epoll_wait` returns and more wakeups per request (the session's old comment mentions
  "spurious" readiness), and the extra wakeups would make every `sendto` more expensive.
- **One run per compiler, on different shared runners.** Run #13 and run #14 did not share a machine, and the 4 CPUs
  are shared with `redis-benchmark`. A change this large is unlikely to be only noise, but it is a profile under
  `perf`, not a benchmark: no reference Redis ran beside it.
- **Lost wakeups are not ruled out.** The run served 1.9 million commands to 50 clients without stalling the whole
  load, which would not show one client waiting for a reply. The stress test, which checks every reply, is the check,
  and it runs in the main CI workflow, not here.

### Stage 1b: CI and benchmark (CI run #75, Benchmark run #29, commit `6f63349`, 2026-11-25)

- **CI:** all 18 jobs passed: Linux gcc and clang in Debug and Release, the AddressSanitizer, UndefinedSanitizer,
  ThreadSanitizer and RealtimeSanitizer builds, the jemalloc build, the macOS jobs, and the static analysis. No lost
  wakeup showed up.
- **Benchmark:** the server's median against the reference Redis in the same job (above 1 is faster than the
  reference), against run #27 before the change:

| Scenario | gcc, run #27 | gcc, run #29 | clang, run #29 |
|---|---:|---:|---:|
| `clients` | 1.00 | 1.00 (163,666 / 164,474) | 1.00 (163,265 / 163,132) |
| `latency` | 1.03 | 0.99 (61,350 / 61,881) | 1.26 (79,302 / 62,972) |
| `pipeline` | 1.09 | 1.06 | 1.12 |
| `bigget` | 1.14 | 1.27 | 1.27 |

**Reading it.** The benchmark does **not** show the doubling that the profile suggested. On `clients` the server and
the reference sit at the same rate (about 164,000) in both jobs, before and after. Equal rates for two different
servers point at a ceiling set by the load generator, not by either server: `redis-benchmark` runs on the same 4 CPUs.
So this benchmark cannot see a drop in the server's CPU per command; the profile measures that, and the profile's
rate under `perf` (187,000) is above the benchmark's, which fits a server that is no longer the limit. `latency` is
one client, whose round trip is mostly the network: gcc shows nothing, clang shows 26 %, and the spread between runs
of that scenario was 61,000 to 85,000 in an earlier run. `bigget` rose against the reference in both jobs, but it
reads pipelined batches and its change is not explained by this one; one run is not enough to claim it.

**What would show it.** A run where the server is the limit: the `SERVER_CPUS` and `CLIENT_CPUS` knobs of
`bench_server.sh` pin the server to one CPU and the load generator to the other three. Then the same run on the
commit before the change, for a same-conditions comparison. The workflow does not pass those knobs yet.

Stage 2: not started.
