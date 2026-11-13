# Use Case: Connect over TCP

## Use Case ID

UC-006

## Description

Client opens a TCP connection to the server and sends requests, as RESP2 arrays (any Redis client) or as inline
commands (telnet, `nc`). The server answers each request, in order, on the same connection.

## Primary Actor

Client (redis-cli, a client library, redis-benchmark, telnet)

## Preconditions

* Server is running and listening on `--host`:`--port` (default `127.0.0.1:6379`)

## Postconditions

* Client is admitted and can send commands until it, or the server, closes the connection
* The connection is counted in `INFO clients` (`connected_clients`) and `INFO stats` (`total_connections_received`)

## Flow of Events

| Step | Actor  | Description                                                                         |
|------|--------|-------------------------------------------------------------------------------------|
| 1    | Client | Connects to `--host`:`--port`                                                       |
| 2    | Server | Accepts the connection and admits it: fewer than `--maxclients` clients are connected |
| 3    | Client | Sends a request, such as `*1\r\n$4\r\nPING\r\n` or the inline line `PING\r\n`       |
| 4    | Server | Reads until it has a whole request; a request split across reads is reassembled     |
| 5    | Server | Executes the command and writes the reply                                           |
| 6    | Client | Reads the reply, and repeats from step 3 or closes the connection                   |

## Alternate Flows

### A1: Pipelined requests

* Client sends several requests without waiting for replies; the server executes them in order and replies in the
  same order, batching what arrived in one read

### A2: Server full

* A connection past `--maxclients` gets `ERR max number of clients reached` and is closed; it is counted in
  `rejected_connections`

### A3: Malformed request

* Server replies `ERR Protocol error: ` followed by what was wrong, then closes the connection; requests before the
  malformed one are still answered

### A4: Unknown command

* Server replies `ERR unknown command '<name>', with args beginning with: ` followed by the first arguments, quoted;
  the connection stays open

### A5: Client past a limit

* A client is disconnected when it buffers more than `--client-query-buffer-limit` bytes of unparsed input, when
  more than `--client-output-buffer-limit` bytes of replies wait to be sent to it, when it sends nothing for
  `--idle-timeout` seconds, or when a request of its is not answered within `--request-timeout` seconds

### A6: Client disconnects

* The server ends the session and frees its slot; replies still pending for it are dropped

## Related Functional Requirements

* FR-008: TCP server and protocol
* FR-009: Concurrent clients
* FR-010: Error replies
* FR-014: Client limits

## Related Nonfunctional Requirements

* NFR-001: Performance (pipelining)
* NFR-002: Security (limits protect the server from a misbehaving client)
* NFR-004: Compatibility (RESP2, inline commands)
* NFR-005: Availability

## Notes

* There is no authentication (FR-020 is planned): bind `--host` to a private address.
* With `--io-threads` above 1, connections are spread over several I/O threads, and commands still run one at a time
  on the keyspace thread.
* Every other client use case starts from a connection made here.
