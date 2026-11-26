# Connection: Test Scenarios

Test scenarios for connecting, `PING`/`ECHO`, request framing and per-client limits, derived from the flows of the
use cases [UC-005](use_cases/uc_005_ping.md) and [UC-006](use_cases/uc_006_connect.md) of the
[Connection feature](README.md) (FR-008, FR-009, FR-014). They extend the
[Master Test Plan](../../test/test_plan_and_report.md), whose section 12 lists FR-008 and FR-009 as covered and FR-014
as unit only.

Scenario IDs are `TC-nnn`, one sequence for the whole project ([ID ranges](../README.md#id-ranges)). Replies are quoted as the server writes them (RESP2 notation: `+PONG` simple string,
`$5` bulk string, `-ERR ...` error). The use cases have no acceptance criteria, so scenarios come from their flows and
alternate flows (A1, A2, ...).

## How to read the coverage columns

Two columns, one question each:

| Column | Meaning |
|--------|---------|
| *Level* | The levels the scenario is designed for. A plan, not a record of what runs |
| *Tested at* | The levels where a test exists today, found from the *Existing test* column: `Unit`, `Integration`, `Stress`, `Differential`, or `none` |
| *Status* | `Covered` when every level in *Level* is in *Tested at*; `Partial` when some are; `Gap` when none are |

So a row with Level `Unit, Integration`, Tested at `Unit` and Status `Partial` has its unit test and lacks its
integration test. `Differential` counts as tested only where the differential script sends the scenario and the run is
gating (the differential step for expiry has gated since CI run 59; see the key-expiry plan).

## Levels and technique

| Level        | Use for                                                                                          |
|--------------|--------------------------------------------------------------------------------------------------|
| Unit         | The parser, the pipeline stages, the guard and a session over a real loopback socket             |
| Integration  | A running server through the test client (`test/integration/`)                                   |
| Stress       | Many clients, fragmented and malformed input, checked against a model (`test/stress/`)           |
| Differential | Replies compared with real Redis; no timing in the request stream                                |
| Fuzz         | Generated input to the two parsers under ASan and UBSan, with properties checked (`test/fuzz/`, section 8). Counts as tested where the CI fuzz job runs the harness and the corpus replay passes under `ctest` |

Timing rules: limits are set to one second in the session tests; assert that the limit fired and that the guard
counted it, not an exact instant.

## 1. PING and ECHO (UC-005)

| ID | Behaviour | Given / When | Expected | Level | Existing test | Tested at | Status |
|-----------|-----------|--------------|----------|-------|---------------|----------|----------|
| TC-160 | `PING` is answered `+PONG` | Connected client; `PING`, then `PING` again | `+PONG` both times, on the same connection | Unit, Integration, Differential | `session`: "A session answers over a real connection"; `ping_pong` TC-1001; `diff_test.py` | Differential, Integration | Partial |
| TC-161 | `PING <message>` echoes it (A1) | `PING HelloWorld` | Bulk string `HelloWorld` | Integration, Differential | `ping_pong` TC-1003 | Integration | Partial |
| TC-162 | `PING` with too many arguments (A2) | `PING a b` | `-ERR wrong number of arguments for 'ping' command` | Unit, Differential | None found | none | Gap |
| TC-163 | `ECHO <message>` echoes it (A6) | `ECHO hello` | Bulk string `hello` | Unit, Differential | `diff_test.py` generates `ECHO <value>` | Differential | Partial |
| TC-164 | `ECHO` takes exactly one argument (A6) | `ECHO`; `ECHO a b` | `-ERR wrong number of arguments for 'echo' command` | Unit, Differential | None found | none | Gap |
| TC-165 | Replies keep request order; clients are independent | Two clients each send `PING One`, `PING Two` | Each client gets `One` then `Two`; no crosstalk | Integration, Stress | `ping_pong` TC-1002, TC-1004; stress phase 2 (values carry client id and sequence number) | Integration, Stress | Covered |
| TC-166 | An idle client is disconnected (A3) | `--idle-timeout` 1 s; the client sends nothing | Server hangs up; `IdleTimeout` violation counted once | Unit | `session`: "A session closes idle clients" | Unit | Covered |
| TC-167 | Activity, or no limit, keeps the client (A3) | Requests every 0.6 s for 4 rounds with a 1 s limit; and no limit with a quiet client | Never closed; no violation counted | Unit | `session`: "A session closes idle clients" (the two other cases) | Unit | Covered |

## 2. Connecting and admission (UC-006 steps 1-2, A2, A6)

| ID | Behaviour | Given / When | Expected | Level | Existing test | Tested at | Status |
|-----------|-----------|--------------|----------|-------|---------------|----------|----------|
| TC-168 | A client connects and is served | Listening server; connect, send, reconnect | Each connection is served | Unit, Integration | `listener`: "A listener accepts clients" | Unit | Partial |
| TC-169 | Many clients at once | Many clients connect together | Every one is served | Unit, Stress | `listener`: "A listener accepts clients"; stress phase 2 | Stress | Partial |
| TC-170 | I/O threads share clients; commands run one at a time (FR-009) | `--io-threads` above 1; many clients each issue commands | Each client served on its own strand; commands never overlap on the keyspace | Unit, Stress | `listener` (several I/O threads); `executor`: "Running work on the keyspace thread"; stress phase 4 | Stress | Partial |
| TC-171 | Admission stops at `--maxclients` | Guard allows 2; admit 2, then a third | Third refused and takes no slot; a freed slot is reusable | Unit | `guard`: "The guard admits clients up to its limit" | Unit | Covered |
| TC-172 | A full server answers and closes | Client past `--maxclients` connects | `-ERR max number of clients reached`, then closed; `rejected_connections` rises | Integration | `guard` (refusal only); `info`: "INFO reports connections when there is a network" (counter set by hand); no test asserts the reply text | Unit | Partial |
| TC-173 | Connections are counted | Admit one, refuse one | `INFO` shows `connected_clients`, `maxclients`, `rejected_connections`, `total_connections_received` | Unit | `info`: "INFO reports connections when there is a network" | Unit | Covered |
| TC-174 | A failed accept does not stop the listener | The next accepts fail, or keep failing | The listener retries with a pause, not in a tight loop, then serves | Unit | `listener`: "A listener survives failed accepts" | Unit | Covered |
| TC-175 | A client that hangs up frees its session (A6) | Client closes the socket | The session ends by itself; the slot is freed | Unit | `session`: "A session answers over a real connection" (hang up case); `guard` (slot reuse) | Unit | Covered |

## 3. Requests and framing (UC-006 steps 3-5, A1, A3, A4)

| ID | Behaviour | Given / When | Expected | Level | Existing test | Tested at | Status |
|-----------|-----------|--------------|----------|-------|---------------|----------|----------|
| TC-176 | RESP array and inline requests | `*1\r\n$4\r\nPING\r\n` and `PING\r\n`; mixed separators and line endings; blank lines | Each is split into its arguments; blank inline lines yield no request and no reply | Unit | `request`: "Parsing inline commands", "Parsing RESP array requests"; `pipeline` (blank inline lines) | Unit | Covered |
| TC-177 | A request split across reads is reassembled | The request cut at every byte, or sent one byte at a time | One request, one reply; nothing consumed until whole | Unit, Stress | `request`: "Parsing RESP arrays outside the bulk-string fast path" (cut at every byte); `pipeline`: "Executing buffered requests"; stress phase 1 | Stress | Partial |
| TC-178 | Pipelined requests run in order (A1) | Several requests in one read, the last one half sent | Complete ones answered in order; the partial one kept and run when the rest arrives | Unit, Stress | `pipeline`: "Splitting the hot path into stages", "Executing buffered requests"; stress phases 2 and 4 | Stress | Partial |
| TC-179 | A malformed request is a protocol error (A3) | `*1\r\n$3\r\nabcXY`; `*abc`; negative or absurd lengths; a valid request first | `-ERR Protocol error: ...`, connection closed; the earlier request was answered | Unit, Stress | `pipeline`; `request`; `parser` (invalid prefix, truncated, bad length); stress phase 1 | Stress | Partial |
| TC-180 | A well-formed value that is not a command | `*0\r\n`; an array holding an integer; then `PING` | Error reply; the connection stays open and `PING` is answered | Unit, Stress | `pipeline`: "Executing buffered requests"; `set_ttl`: "Malformed requests get RESP error replies"; stress phase 1 ("empty array") | Stress | Partial |
| TC-181 | Unknown command (A4) | `NOPE a b` | `-ERR unknown command 'NOPE', with args beginning with: 'a' 'b' `; connection open; the echo stops at 128 bytes | Unit, Stress, Differential | `set_ttl`: "Malformed requests get RESP error replies", "Command names are matched without regard to case"; stress phase 1; `diff_test.py` | Differential, Stress | Partial |
| TC-182 | Wrong arity | `GET a b` | `-ERR wrong number of arguments for 'get' command`; connection open | Unit, Stress, Differential | `set_ttl`: "Malformed requests get RESP error replies"; stress phase 1; `diff_test.py` | Differential, Stress | Partial |
| TC-183 | Command names ignore case | `get`, `GeT`, `strlen` | Dispatched; arity errors name the command in lower case | Unit | `set_ttl`: "Command names are matched without regard to case" | Unit | Covered |
| TC-184 | Declared lengths and nesting are bounded | Oversized bulk length; huge array length; deep nesting; over-long inline line | Rejected; nothing allocated up front; no stack overflow | Unit | `parser`: "Parser rejects or bounds client-declared lengths", "Parser bounds array nesting"; `request`: inline command longer than the limit | Unit | Covered |
| TC-321 | A length with a leading zero is a protocol error, however the request is split ([#52](https://github.com/sleepyviper1213/redis/issues/52)) | `*2\r\n$04\r\nECHO\r\n$2\r\nhi\r\n` whole and cut at every byte; `*02` as an array length; `$0\r\n\r\n` (still valid) | `PARSE_NUMBER` once the length line is complete, an incomplete error before it, never a command; as Redis (`string2ll`) | FR-008, NFR-004 | Unit, Fuzz | `request`: "Parsing request lengths and payloads" (leading zeros); fuzz corpus `lead_zero_bulk_length` (found by TC-307) | Unit | Partial |
| TC-185 | Payloads are binary-safe | Bulk payload with CRLF and NUL | Cut by its length, not by line breaks | Unit, Stress | `request`: "Parsing request lengths and payloads"; stress phase 2 (binary values) | Stress | Partial |
| TC-186 | Garbage input does not kill the server | 200 connections of random bytes, some opened and dropped at once | The server keeps serving afterwards | Stress | stress phase 1 (garbage and vanishing connections) | Stress | Covered |

## 4. Limits per client (UC-006 A5, FR-014)

| ID | Behaviour | Given / When | Expected | Level | Existing test | Tested at | Status |
|-----------|-----------|--------------|----------|-------|---------------|----------|----------|
| TC-187 | Query buffer limit | A client buffers more than `--client-query-buffer-limit` unparsed bytes | Disconnected; violation counted by kind | Unit, Integration | `guard`: "The guard bounds the buffers" (limit arithmetic and counting only; no session-level case) | Unit | Partial |
| TC-188 | Output buffer limit | Pipelined `GET x` x 10 of a 100-byte value, limit 200 bytes | Execution stops after the reply that crossed the limit; with room, every request runs; the client is then dropped | Unit | `guard`: "A batch stops once its replies pass the output limit" (the drop itself is not asserted) | Unit | Covered |
| TC-189 | Request timeout, request queued | Limit 1 s; keyspace thread busy 2 s; the client sends a write | Server hangs up and counts `RequestTimeout`; the write never runs | Unit | `session`: "A session closes clients whose requests are not answered in time" | Unit | Covered |
| TC-190 | Request timeout, client that does not read | Limit 1 s; a 4 MiB value requested and left unread | Write abandoned, connection closed, violation counted | Unit | `session`: same scenario; "A session stalled on a client that does not read still stops" (no limit; stops on shutdown) | Unit | Covered |
| TC-191 | A large reply arrives whole | 4 MiB value read back by a reading client | The socket takes part at once and the rest follows; value intact | Unit | `session`: "A reply larger than the socket buffer arrives whole" | Unit | Covered |
| TC-271 | Admission holds under contention (FR-014, NFR-002) | Guard allows 4; 16 threads released together race for slots, then churn admit and release | Exactly 4 admitted while all tickets are held; concurrent holders never exceed 4; `clients()` is 0 at the end | Unit | `guard`: "The guard never admits more clients than its limit under contention" | Unit | Covered |
| TC-192 | Limits are observable on a live server | Real server with `--maxclients 1`; a second client connects; read `INFO` | Second client refused with the error of TC-172; `INFO` counts it | Integration | None found (master plan A-7) | none | Gap |

Coverage per use case is in [traceability.md](traceability.md).

## 5. RESP encoding and decoding (`common/resp`, FR-008, NFR-004)

The reply side and the request side of the wire format. These are the parser's own unit tests, with the IDs they
were given before the TC scheme: they keep their numbers, which the project does not renumber.

| ID | Scenario | Given / When | Then | Req | Level | Existing test | Tested at | Status |
|----|----------|--------------|------|-----|-------|---------------|----------|----------|
| TC-2001 | Encode RESP values into wire format | A value of each RESP type (simple string, error, integer, bulk, null, array) is encoded | The exact bytes of RESP2, with CRLF terminators | FR-008, NFR-004 | Unit | `parser`: "Encode RESP values into wire format" | Unit | Covered |
| TC-2002 | Decode RESP wire format into values | The wire bytes of each RESP type are decoded | A value of the same type and contents; malformed bytes are errors (TC-179, TC-184) | FR-008 | Unit | `parser`: "Decode RESP wire format into values" | Unit | Covered |

## 6. The conversation and the stop-time close ([ADR-0012](adr/0012-session-decisions-apart-from-io.md), DX-81)

What a session decides, tested without a socket, a scheduler or a clock; and the one class of the session that
takes a mutex, tested on its own. Section 6.1 is covered by `conversation.test.cpp`; 6.2 is still a `Gap`. They sit beside the loopback scenarios of sections 1 to 4, which keep what only a
socket shows (cancellation, partial writes, shutdown); where a row below also closes a Partial there, it says so.

A conversation test builds a real `NetGuard`, `CommandHandler` and `Database` (all cheap, no thread) and feeds it
strings through `read_buffer()` and `ingest_read_bytes()`. Each behaviour is asserted in both modes, **inline** and
**threaded** (`Parts::is_threaded`), unless the row says otherwise: in the threaded mode the test calls `execute_ingested_requests()`
itself, on its own thread, as the keyspace thread would.

### 6.1 Conversation (`Conversation`, `src/server/net/detail/conversation.hpp`)

| ID | Behaviour | Given / When | Expected | Req | Level | Existing test | Tested at | Status |
|----|-----------|--------------|----------|-----|-------|---------------|-----------|--------|
| TC-278 | A read of nothing changes nothing | `ingest_read_bytes(0)` after a half-sent request is pending | Returns false; the pending bytes are kept; no violation counted | FR-008 | Unit | `conversation`: "A conversation takes in what a client sends" | Unit | Covered |
| TC-279 | A complete request is answered | `PING\r\n`, then `execute_ingested_requests()` | `ingest_read_bytes` returns true; `unsent_replies()` is `+PONG\r\n`; `is_over()` is false; `discard_sent_replies()` empties it | FR-008 | Unit | `conversation`: "A conversation takes in what a client sends" | Unit | Covered |
| TC-280 | A partial request gets no reply and is kept | `*1\r\n$4\r\nPI`, run, then `NG\r\n`, run | First round: no replies, nothing consumed (threaded: `ingest_read_bytes` is false); second round: `+PONG` | FR-008 | Unit | `conversation`: "A conversation takes in what a client sends" | Unit | Covered |
| TC-281 | Pipelined requests run in order in one batch | `PING a`, `PING b`, `PING c` in one read, then one more read | Replies `a`, `b`, `c` in order in one `unsent_replies()`; the first batch's bytes no longer count against the query limit when the second read arrives (limit 64, 2 x ~50 bytes), which shows they were dropped (ADR-0004) | FR-008 | Unit | `conversation`: "A conversation answers pipelined requests in order" | Unit | Covered |
| TC-282 | Inline and threaded modes answer alike | The same byte stream (valid requests, a partial one, a protocol error), cut at every byte, once per mode | Identical reply bytes and the same `is_over()` in both modes, whatever the cut | FR-008, FR-009 | Unit | `conversation`: "Inline and threaded conversations answer alike" | Unit | Covered |
| TC-283 | The query buffer limit ends the conversation (A5) | Limit 64 bytes; a pending 64-byte partial request, then one more byte | At 64 bytes `ingest_read_bytes` accepts; at 65 it returns false, `is_over()` is true and the guard counted one `QueryBufferFull` with the peer and the size. Closes the session-level gap of TC-187 | FR-014 | Unit | `conversation`: "A conversation ends when a client breaks a limit" (the guard test keeps the arithmetic) | Unit | Covered |
| TC-284 | The output buffer limit drops the replies and ends the conversation | Limit 200 bytes; `GET x` x 10 of a 100-byte value | Execution stops after the reply that crossed the limit; `unsent_replies()` is empty, `is_over()` is true, one `OutputBufferFull` counted. With room, all 10 run and the conversation stays open. Closes the "drop not asserted" gap of TC-188 | FR-014 | Unit | `conversation`: "A conversation ends when a client breaks a limit" (the guard test keeps the arithmetic) | Unit | Covered |
| TC-285 | A protocol error ends the conversation after the replies before it | `PING ok` then `*1\r\n$3\r\nabcXY` | `unsent_replies()` holds the first request's reply, then `-ERR Protocol error: ...`; `is_over()` is true once they are sent; nothing counted on the guard | FR-008 | Unit | `conversation`: "A protocol error ends a conversation after the replies before it" | Unit | Covered |
| TC-286 | A value that is not a command is answered and the conversation stays open | `*0\r\n`, then `PING` | An error reply, then `+PONG`; `is_over()` is false | FR-008 | Unit | `conversation`: "A conversation takes in what a client sends" | Unit | Covered |
| TC-287 | An idle timeout ends the conversation | `count_idle_timeout_and_end(30s)` | `is_over()` is true; one `IdleTimeout` counted with the peer and `30` | FR-014 | Unit | `conversation`: "Timeouts and failures end a conversation" | Unit | Covered |
| TC-288 | A request timeout drops what was not sent | Replies pending; `count_request_timeout(1s)`, then `drop_replies_and_end()` once the work unwinds | counted at once with `unsent_replies()` and `is_over()` unchanged (work may still be unwinding); after `drop_replies_and_end()`, `unsent_replies()` is empty and `is_over()` is true, still one `RequestTimeout` | FR-014 | Unit | `conversation`: "Timeouts and failures end a conversation" | Unit | Covered |
| TC-289 | A failed batch sends nothing | Replies half built; `drop_replies_and_end()` | `unsent_replies()` is empty, `is_over()` is true; no violation counted (the failure is logged by the session) | FR-014 | Unit | `conversation`: "Timeouts and failures end a conversation" | Unit | Covered |
| TC-290 | Which I/O failures are news | `report_for` with end of file; with another error and the socket closed; with another error and the socket open | `ClientLeft`; `AfterServerClose`; `Failure`. End of file wins over a closed socket | FR-014 | Unit | `conversation`: "A failed read or write is told about as it deserves" | Unit | Covered |

### 6.2 CloseSocketOnStop (`src/server/net/detail/session.hpp`)

The mutex, the stop callback that can fire on any thread, and the posted close that may run after the session is
gone. A test uses a socket on a single-threaded `io_context` the test runs by hand, and a stop source it controls.
Rows marked TSan are meant to run under the `tsan` preset, the others under every preset.

| ID | Behaviour | Given / When | Expected | Req | Level | Existing test | Tested at | Status |
|----|-----------|--------------|----------|-----|-------|---------------|-----------|--------|
| TC-291 | A stop before `arm()` closes at once | The stop is requested, then `arm()` | After the context runs, the socket is closed | FR-014 | Unit | None | none | Gap |
| TC-292 | A stop after `arm()` closes on the socket's executor, not the caller's thread | Armed; another thread requests the stop | The close runs inside the test's `io_context::run()`, never on the stopping thread; the socket is closed afterwards | FR-014 | Unit, TSan | None | none | Gap |
| TC-293 | A close still queued when the session ends finds nothing | Stop requested; the `CloseSocketOnStop` destroyed before the context runs | Running the context afterwards is harmless: no use of the destroyed object (ASan, TSan) | FR-014 | Unit, TSan | None | none | Gap |
| TC-294 | A stop racing the destructor is safe | Many rounds: one thread requests the stop while the test destroys the object | No data race, no crash; the socket is either closed or untouched | FR-014 | Unit, TSan | None | none | Gap |

Open for review: `Target` holds a `tcp::socket*`. TC-291 to TC-294 work with a real socket. Making `Target` generic
over "anything with `close(error_code&)`" would let them run without one, at the cost of a template on a class that is
not templated today. The plan assumes the real socket until that is decided.

## 7. The I/O backend ([io_uring_experiment.md](io_uring_experiment.md))

| ID | Behaviour | Given / When | Expected | Level | Existing test | Tested at | Status |
|----|-----------|--------------|----------|-------|---------------|-----------|--------|
| TC-298 | Measured: what a non-pipelined request costs, and what io_uring changes | Stage 1: the Profile workflow on the `clients` scenario (50 clients, no pipelining), `perf` with call graphs and counters. Stage 2, if stage 1 says so: the Benchmark workflow with an io_uring build beside the epoll one, and the suites on that build | Stage 1: the kernel's share of the server's CPU, and context switches per 1000 commands, recorded. Stage 2: the `clients` median at least 10 % higher in repeated runs, `pipeline` not lower, every test passing on the io_uring build; the option stays off by default | Benchmark | `scripts/profile_server_linux.sh` and `.github/workflows/profile.yml` (stage 1, `clients` step; ran once, results in io_uring_experiment.md); stage 1b (the read without `epoll_ctl`): profiled in run #14, CI and the Benchmark workflow passed, but the benchmark cannot see the gain; stage 1c (an A/B of the base and the new revision with the `saturate` scenario of `bench_server.sh`): run twice, +4 to 5 % at saturation, in io_uring_experiment.md (the workflow that ran it was removed afterwards); stage 2 not built | none | Gap |
| TC-299 | The `io_context` is configured as before, plus the reactor setting on epoll | `io_config(n)` for 1, 2 and 4 I/O threads, against an `io_context` built from the same number as a concurrency hint | The scheduler's concurrency hint and locking, and the reactor's I/O and registration locking, read back the same; `reactor.reset_edge_on_partial_read` is 1 where Asio uses epoll and 0 elsewhere | Unit | `io_config`: "The io_context is configured as it was, plus the reactor setting" (the epoll expectation is compiled only where Asio uses epoll: Linux CI) | Unit | Covered |

## 8. Fuzzing the parsers ([#51](https://github.com/sleepyviper1213/redis/issues/51), DX-86)

The two functions that read untrusted bytes, fed input that libFuzzer generates and mutates from a seed corpus and a
RESP dictionary. A failure is a sanitizer report, an abort (`REDIS_VERIFY`, a failed property) or a timeout. Every
crashing input is minimised and added to the corpus, and the corpus is replayed by `ctest` in every preset, so it stays a
regression test. Sections 3 and 5 keep the hand-picked cases. These rows check that no input outside those cases breaks
the same rules.

| ID | Behaviour | Given / When | Expected | Req | Level | Existing test | Tested at | Status |
|----|-----------|--------------|----------|-----|-------|---------------|-----------|--------|
| TC-300 | `Parser::parse` is total and bounded on any input | Any byte string, up to 64 KiB | Returns a `Value` or a `RespError`; no sanitizer report, no abort, no stack overflow (nesting stops at `MAX_DEPTH`); no allocation larger than the input plus `MAX_ARRAY_RESERVE` elements (`-rss_limit_mb` and `-malloc_limit_mb`) | NFR-002, FR-008 | Fuzz | `test/fuzz/resp_parser.fuzz.cpp` (planned) | none | Gap |
| TC-307 | `parse_request` frames any byte stream consistently | Any byte string, cut into requests by calling `parse_request` until it fails, as a session does; the first byte also picks a cut point `k` | On success `pos` strictly advances and stays within the input; on error `pos` and `args` are unchanged. Parsing only the first `k` bytes never yields a different first request than the whole input, and fails only with an incomplete error (`Parser::is_incomplete`) or with the same error as the whole input (TC-177 for every stream, not only the listed ones) | NFR-002, FR-008 | Fuzz | `test/fuzz/resp_request.fuzz.cpp` (planned) | none | Gap |
| TC-308 | Encoding a parsed value is idempotent | Any input that `Parser::parse` accepts | `encode(parse(encode(v)))` equals `encode(v)` byte for byte, where `v = parse(input)`: the encoder never writes what the parser cannot read back (TC-2001, TC-2002 for every value) | FR-008, NFR-004 | Fuzz | `test/fuzz/resp_parser.fuzz.cpp` (planned) | none | Gap |

How it runs: `cmake --workflow --preset x86_64-linux-gnu-clang-fuzz` builds with libFuzzer and ASan, and replays the
corpus. The CI job `fuzz` then runs each harness for a fixed budget (`-max_total_time`) and uploads any crashing input.
Other presets build the same harnesses with the replay driver and run only the corpus.

## Entry and exit criteria

* **Entry**: the server builds; the unit and integration suites run in the chosen preset; the stress binary is built.
* **Exit**: every scenario is Unit or higher, or has a Gap recorded in the test plan's anomaly list (section 16); the
  session and listener scenarios pass under the `tsan` preset.
