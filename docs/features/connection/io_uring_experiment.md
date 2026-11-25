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

None yet.
