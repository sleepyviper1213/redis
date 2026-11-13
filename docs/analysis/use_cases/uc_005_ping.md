# Use Case: Client Connection Ping-Pong

## Use Case ID

UC-005

## Description

Client checks that the server is alive, and that its connection still works, by sending `PING`.

## Primary Actor

Client (redis-cli, a client library's health check, a load balancer probe)

## Preconditions

* Server is running and listening on `--host`:`--port`
* Client is connected, and was admitted (fewer than `--maxclients` clients); see
  [UC-006](uc_006_connect.md)

## Postconditions

* Client knows the server answered; nothing changes on the server

## Flow of Events

| Step | Actor  | Description                                   |
|------|--------|-----------------------------------------------|
| 1    | Client | Sends `PING`                                  |
| 2    | Server | Parses the request and checks its arity       |
| 3    | Server | Replies `+PONG`                               |
| 4    | Client | Receives the reply                            |

## Alternate Flows

### A1: PING with a message

* `PING <message>` is answered with the message, as a bulk string

### A2: Too many arguments

* Server replies: `ERR wrong number of arguments for 'ping' command`

### A3: Idle client

* With `--idle-timeout` set, a client that sends nothing for that long is disconnected; a periodic `PING` keeps it
  connected

### A4: Server full

* A connection past `--maxclients` gets `ERR max number of clients reached` and is closed

### A5: Connection lost

* No reply arrives; the client's own timeout detects it and reconnects

### A6: `ECHO`

* `ECHO <message>` is answered with the message, as a bulk string; it takes exactly one argument

## Related Functional Requirements

* FR-007: Server commands
* FR-008: TCP server and protocol
* FR-014: Client limits

## Related Nonfunctional Requirements

* NFR-001: Performance
* NFR-005: Availability

## Notes

* A slow command on the server delays every reply, `PING` included, until it ends; the command watchdog logs such
  commands ([UC-018](uc_018_watch_logs.md), FR-016).
