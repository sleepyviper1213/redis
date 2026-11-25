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

Stage 2: not started.
