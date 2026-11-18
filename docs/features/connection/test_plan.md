# Connection: Test Scenarios

Test scenarios for connecting, `PING`/`ECHO`, request framing and per-client limits, derived from the flows of the
use cases [UC-005](use_cases/uc_005_ping.md) and [UC-006](use_cases/uc_006_connect.md) of the
[Connection feature](README.md) (FR-008, FR-009, FR-014). They extend the
[Master Test Plan](../../test/test_plan_and_report.md), whose section 12 lists FR-008 and FR-009 as covered and FR-014
as unit only.

Scenario IDs are `TC-nnn`, one sequence for the whole project ([ID ranges](../README.md#id-ranges)). Replies are quoted as the server writes them (RESP2 notation: `+PONG` simple string,
`$5` bulk string, `-ERR ...` error). The use cases have no acceptance criteria, so scenarios come from their flows and
alternate flows (A1, A2, ...).

## How to read the coverage column

| Mark         | Meaning                                                                                   |
|--------------|-------------------------------------------------------------------------------------------|
| Unit         | A unit test whose scenario name matches; named in the *Existing test* column              |
| Integration  | Only an integration test (a running server through the test client) matches               |
| Stress       | Only a phase of `test/stress/stress.cpp` matches                                          |
| Differential | Only the differential script `scripts/diff_test.py` sends it                              |
| Partial      | A test covers part of the scenario                                                        |
| Gap          | No test found; to be written at the level in the *Level* column                           |

Coverage was matched by test and scenario names in `test/`, not by reading every assertion; confirm when a scenario
is written or run.

## Levels and technique

| Level        | Use for                                                                                          |
|--------------|--------------------------------------------------------------------------------------------------|
| Unit         | The parser, the pipeline stages, the guard and a session over a real loopback socket             |
| Integration  | A running server through the test client (`test/integration/`)                                   |
| Stress       | Many clients, fragmented and malformed input, checked against a model (`test/stress/`)           |
| Differential | Replies compared with real Redis; no timing in the request stream                                |

Timing rules: limits are set to one second in the session tests; assert that the limit fired and that the guard
counted it, not an exact instant.

## 1. PING and ECHO (UC-005)

| ID        | Behaviour | Given / When | Expected | Level | Existing test | Coverage |
|-----------|-----------|--------------|----------|-------|---------------|----------|
| TC-160 | `PING` is answered `+PONG` | Connected client; `PING`, then `PING` again | `+PONG` both times, on the same connection | Unit, Integration, Differential | `session`: "A session answers over a real connection"; `ping_pong` TC-1001; `diff_test.py` | Unit |
| TC-161 | `PING <message>` echoes it (A1) | `PING HelloWorld` | Bulk string `HelloWorld` | Integration, Differential | `ping_pong` TC-1003 | Integration |
| TC-162 | `PING` with too many arguments (A2) | `PING a b` | `-ERR wrong number of arguments for 'ping' command` | Unit, Differential | None found | Gap |
| TC-163 | `ECHO <message>` echoes it (A6) | `ECHO hello` | Bulk string `hello` | Unit, Differential | `diff_test.py` generates `ECHO <value>` | Differential |
| TC-164 | `ECHO` takes exactly one argument (A6) | `ECHO`; `ECHO a b` | `-ERR wrong number of arguments for 'echo' command` | Unit, Differential | None found | Gap |
| TC-165 | Replies keep request order; clients are independent | Two clients each send `PING One`, `PING Two` | Each client gets `One` then `Two`; no crosstalk | Integration, Stress | `ping_pong` TC-1002, TC-1004; stress phase 2 (values carry client id and sequence number) | Integration |
| TC-166 | An idle client is disconnected (A3) | `--idle-timeout` 1 s; the client sends nothing | Server hangs up; `IdleTimeout` violation counted once | Unit | `session`: "A session closes idle clients" | Unit |
| TC-167 | Activity, or no limit, keeps the client (A3) | Requests every 0.6 s for 4 rounds with a 1 s limit; and no limit with a quiet client | Never closed; no violation counted | Unit | `session`: "A session closes idle clients" (the two other cases) | Unit |

## 2. Connecting and admission (UC-006 steps 1-2, A2, A6)

| ID        | Behaviour | Given / When | Expected | Level | Existing test | Coverage |
|-----------|-----------|--------------|----------|-------|---------------|----------|
| TC-168 | A client connects and is served | Listening server; connect, send, reconnect | Each connection is served | Unit, Integration | `listener`: "A listener accepts clients" | Unit |
| TC-169 | Many clients at once | Many clients connect together | Every one is served | Unit, Stress | `listener`: "A listener accepts clients"; stress phase 2 | Unit |
| TC-170 | I/O threads share clients; commands run one at a time (FR-009) | `--io-threads` above 1; many clients each issue commands | Each client served on its own strand; commands never overlap on the keyspace | Unit, Stress | `listener` (several I/O threads); `executor`: "Running work on the keyspace thread"; stress phase 4 | Unit |
| TC-171 | Admission stops at `--maxclients` | Guard allows 2; admit 2, then a third | Third refused and takes no slot; a freed slot is reusable | Unit | `guard`: "The guard admits clients up to its limit" | Unit |
| TC-172 | A full server answers and closes | Client past `--maxclients` connects | `-ERR max number of clients reached`, then closed; `rejected_connections` rises | Integration | `guard` (refusal only); `info`: "INFO reports connections when there is a network" (counter set by hand); no test asserts the reply text | Partial |
| TC-173 | Connections are counted | Admit one, refuse one | `INFO` shows `connected_clients`, `maxclients`, `rejected_connections`, `total_connections_received` | Unit | `info`: "INFO reports connections when there is a network" | Unit |
| TC-174 | A failed accept does not stop the listener | The next accepts fail, or keep failing | The listener retries with a pause, not in a tight loop, then serves | Unit | `listener`: "A listener survives failed accepts" | Unit |
| TC-175 | A client that hangs up frees its session (A6) | Client closes the socket | The session ends by itself; the slot is freed | Unit | `session`: "A session answers over a real connection" (hang up case); `guard` (slot reuse) | Unit |

## 3. Requests and framing (UC-006 steps 3-5, A1, A3, A4)

| ID        | Behaviour | Given / When | Expected | Level | Existing test | Coverage |
|-----------|-----------|--------------|----------|-------|---------------|----------|
| TC-176 | RESP array and inline requests | `*1\r\n$4\r\nPING\r\n` and `PING\r\n`; mixed separators and line endings; blank lines | Each is split into its arguments; blank inline lines yield no request and no reply | Unit | `request`: "Parsing inline commands", "Parsing RESP array requests"; `pipeline` (blank inline lines) | Unit |
| TC-177 | A request split across reads is reassembled | The request cut at every byte, or sent one byte at a time | One request, one reply; nothing consumed until whole | Unit, Stress | `request`: "Parsing RESP arrays outside the bulk-string fast path" (cut at every byte); `pipeline`: "Executing buffered requests"; stress phase 1 | Unit |
| TC-178 | Pipelined requests run in order (A1) | Several requests in one read, the last one half sent | Complete ones answered in order; the partial one kept and run when the rest arrives | Unit, Stress | `pipeline`: "Splitting the hot path into stages", "Executing buffered requests"; stress phases 2 and 4 | Unit |
| TC-179 | A malformed request is a protocol error (A3) | `*1\r\n$3\r\nabcXY`; `*abc`; negative or absurd lengths; a valid request first | `-ERR Protocol error: ...`, connection closed; the earlier request was answered | Unit, Stress | `pipeline`; `request`; `parser` (invalid prefix, truncated, bad length); stress phase 1 | Unit |
| TC-180 | A well-formed value that is not a command | `*0\r\n`; an array holding an integer; then `PING` | Error reply; the connection stays open and `PING` is answered | Unit, Stress | `pipeline`: "Executing buffered requests"; `set_ttl`: "Malformed requests get RESP error replies"; stress phase 1 ("empty array") | Unit |
| TC-181 | Unknown command (A4) | `NOPE a b` | `-ERR unknown command 'NOPE', with args beginning with: 'a' 'b' `; connection open; the echo stops at 128 bytes | Unit, Stress, Differential | `set_ttl`: "Malformed requests get RESP error replies", "Command names are matched without regard to case"; stress phase 1; `diff_test.py` | Unit |
| TC-182 | Wrong arity | `GET a b` | `-ERR wrong number of arguments for 'get' command`; connection open | Unit, Stress, Differential | `set_ttl`: "Malformed requests get RESP error replies"; stress phase 1; `diff_test.py` | Unit |
| TC-183 | Command names ignore case | `get`, `GeT`, `strlen` | Dispatched; arity errors name the command in lower case | Unit | `set_ttl`: "Command names are matched without regard to case" | Unit |
| TC-184 | Declared lengths and nesting are bounded | Oversized bulk length; huge array length; deep nesting; over-long inline line | Rejected; nothing allocated up front; no stack overflow | Unit | `parser`: "Parser rejects or bounds client-declared lengths", "Parser bounds array nesting"; `request`: inline command longer than the limit | Unit |
| TC-185 | Payloads are binary-safe | Bulk payload with CRLF and NUL | Cut by its length, not by line breaks | Unit, Stress | `request`: "Parsing request lengths and payloads"; stress phase 2 (binary values) | Unit |
| TC-186 | Garbage input does not kill the server | 200 connections of random bytes, some opened and dropped at once | The server keeps serving afterwards | Stress | stress phase 1 (garbage and vanishing connections) | Stress |

## 4. Limits per client (UC-006 A5, FR-014)

| ID        | Behaviour | Given / When | Expected | Level | Existing test | Coverage |
|-----------|-----------|--------------|----------|-------|---------------|----------|
| TC-187 | Query buffer limit | A client buffers more than `--client-query-buffer-limit` unparsed bytes | Disconnected; violation counted by kind | Unit, Integration | `guard`: "The guard bounds the buffers" (limit arithmetic and counting only; no session-level case) | Partial |
| TC-188 | Output buffer limit | Pipelined `GET x` x 10 of a 100-byte value, limit 200 bytes | Execution stops after the reply that crossed the limit; with room, every request runs; the client is then dropped | Unit | `guard`: "A batch stops once its replies pass the output limit" (the drop itself is not asserted) | Partial |
| TC-189 | Request timeout, request queued | Limit 1 s; keyspace thread busy 2 s; the client sends a write | Server hangs up and counts `RequestTimeout`; the write never runs | Unit | `session`: "A session closes clients whose requests are not answered in time" | Unit |
| TC-190 | Request timeout, client that does not read | Limit 1 s; a 4 MiB value requested and left unread | Write abandoned, connection closed, violation counted | Unit | `session`: same scenario; "A session stalled on a client that does not read still stops" (no limit; stops on shutdown) | Unit |
| TC-191 | A large reply arrives whole | 4 MiB value read back by a reading client | The socket takes part at once and the rest follows; value intact | Unit | `session`: "A reply larger than the socket buffer arrives whole" | Unit |
| TC-192 | Limits are observable on a live server | Real server with `--maxclients 1`; a second client connects; read `INFO` | Second client refused with the error of TC-172; `INFO` counts it | Integration | None found (master plan A-7) | Gap |

Coverage per use case is in [traceability.md](traceability.md).

## 5. RESP encoding and decoding (`common/resp`, FR-008, NFR-004)

The reply side and the request side of the wire format. These are the parser's own unit tests, with the IDs they
were given before the TC scheme: they keep their numbers, which the project does not renumber.

| ID | Scenario | Given / When | Then | Req | Level | Existing test | Coverage |
|----|----------|--------------|------|-----|-------|---------------|----------|
| TC-2001 | Encode RESP values into wire format | A value of each RESP type (simple string, error, integer, bulk, null, array) is encoded | The exact bytes of RESP2, with CRLF terminators | FR-008, NFR-004 | Unit | `parser`: "Encode RESP values into wire format" | Unit |
| TC-2002 | Decode RESP wire format into values | The wire bytes of each RESP type are decoded | A value of the same type and contents; malformed bytes are errors (TC-179, TC-184) | FR-008 | Unit | `parser`: "Decode RESP wire format into values" | Unit |

## Entry and exit criteria

* **Entry**: the server builds; the unit and integration suites run in the chosen preset; the stress binary is built.
* **Exit**: every scenario is Unit or higher, or has a Gap recorded in the test plan's anomaly list (section 16); the
  session and listener scenarios pass under the `tsan` preset.
