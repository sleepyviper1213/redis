
# Use Case: Save Snapshot

## Use Case ID

UC-001

## Description

Client requests the server to persist the in-memory database to disk using the `SAVE` command.

## Primary Actor

Client (via Python console or HTTP interface)

## Preconditions

* Server is running
* The database contains at least one key-value pair
* Command syntax is valid: `SAVE`

## Postconditions

* A snapshot file (`redis.snap`) is created in the project directory
* If an older snapshot exists, it's renamed to `redis.snap.bak`

## Flow of Events

| Step | Actor  | Description                      |
|------|--------|----------------------------------|
| 1    | Client | Sends `SAVE` command             |
| 2    | Server | Validates command                |
| 3    | Server | Runs file-write snapshot routine |
| 4    | Server | Writes snapshot to disk          |
| 5    | Server | Responds with success or error   |

## Alternate Flows

### A1: Invalid Command Format

* Server replies: `ERR wrong number of arguments for 'SAVE'`

### A2: Fork or I/O Error

* Server replies with relevant failure message

## Related Functional Requirements

* FR-4: SAVE
* FR-7: Snapshot File
* FR-8: Backup on Save

## Related Nonfunctional Requirements

* NFR-3.4.1: Performance (Snapshot latency ≤ 100ms)
* NFR-3.4.2: Security (No overwrite without `.bak`)
* NFR-3.4.3: Reliability (Crash recovery from latest snapshot)
* NFR-3.4.5: Availability (Server remains responsive during snapshot)

## Notes

* Snapshotting is currently blocking. Future versions may implement background saving.

---

# Use Case: Set Key-Value Pair

## Use Case ID

UC-002

## Description

Client stores or updates a key-value pair in the in-memory database using the `SET` command.

## Primary Actor

Client (Python console or CLI)

## Preconditions

* Server is running
* Command syntax is valid: `SET <key> <value>`

## Postconditions

* The key-value pair is stored or updated in memory
* Any existing value for the key is overwritten

## Flow of Events

| Step | Actor  | Description                               |
| ---- | ------ | ----------------------------------------- |
| 1    | Client | Sends `SET <key> <value>`                 |
| 2    | Server | Parses and validates command              |
| 3    | Server | Inserts or updates the key with new value |
| 4    | Server | Responds with `"OK"`                      |

## Alternate Flows

### A1: Invalid Command Format

* Server responds with: `ERR wrong number of arguments for 'SET'`

### A2: Value Contains Spaces

* Client should wrap value in quotes or use encoding

## Related Functional Requirements

* FR-2: SET
* FR-13: Error Responses

## Related Nonfunctional Requirements

* NFR-3.4.1: Performance (≤ 1ms latency)
* NFR-3.4.6: Maintainability
* NFR-3.4.8: Extensibility

## Notes

* Only string keys and values are supported. No TTL or expiration implemented.

---

# Use Case: Get Key Value

## Use Case ID

UC-003

## Description

Client retrieves the value for a key using the `GET` command.

## Primary Actor

Client (Python console or CLI)

## Preconditions

* Server is running
* Command syntax is valid: `GET <key>`
* Key-value store is initialized

## Postconditions

* If key exists and is valid, value is returned
* If key does not exist or is expired, nil or error is returned

## Flow of Events

| Step | Actor  | Description                     |
| ---- | ------ | ------------------------------- |
| 1    | Client | Sends `GET <key>`               |
| 2    | Server | Parses and validates command    |
| 3    | Server | Looks up key and returns result |

## Alternate Flows

### A1: Key Expired

* Server deletes key and responds with nil

### A2: Invalid Format

* Respond with: `ERR wrong number of arguments for 'GET'`

## Related Functional Requirements

* FR-1: GET
* FR-13: Error Responses

## Related Nonfunctional Requirements

* NFR-3.4.1: Performance (≤ 1ms latency)
* NFR-3.4.3: Reliability
* NFR-3.4.6: Maintainability

## Notes

* Keys are strings only. No support for complex types.

---

# Use Case: Delete Key(s)

## Use Case ID

UC-004

## Description

Client deletes one or more keys from the in-memory store using the `DEL` command.

## Primary Actor

Client (Python console or CLI)

## Preconditions

* Server is running
* Command syntax is valid: `DEL <key1> <key2> ...`

## Postconditions

* All specified keys that exist are removed
* Response returns number of keys deleted

## Flow of Events

| Step | Actor  | Description                                   |
| ---- | ------ | --------------------------------------------- |
| 1    | Client | Sends `DEL <key1> <key2> ...`                 |
| 2    | Server | Parses command and extracts key list          |
| 3    | Server | Deletes all found keys, ignoring missing ones |
| 4    | Server | Returns number of keys deleted                |

## Alternate Flows

### A1: No Keys Given

* Respond with: `ERR wrong number of arguments for 'DEL'`

### A2: Some Keys Missing

* Server deletes existing keys, skips missing ones

## Related Functional Requirements

* FR-3: DEL
* FR-13: Error Responses

## Related Nonfunctional Requirements

* NFR-3.4.1: Performance
* NFR-3.4.6: Maintainability

## Notes

* The command is idempotent. Missing keys do not cause failure.
  Here’s the **use case description** for a client connection ping-pong, following the same structure and formatting as your example:

---

# Use Case: Client Connection Ping-Pong

## Use Case ID

UC-005

## Description

Client maintains an active connection with the server by periodically sending `PING` commands and receiving `PONG` responses to verify liveness.

## Primary Actor

Client (e.g., Redis CLI, custom client application)

## Preconditions

* Server is running and listening on the configured port
* Client is connected to the server via TCP socket
* Command syntax is valid: `PING`

## Postconditions

* Server responds with `PONG` for each `PING`
* Connection health is verified
* If server does not respond, client detects connection failure

## Flow of Events

| Step | Actor  | Description                           |
| ---- | ------ | ------------------------------------- |
| 1    | Client | Sends `PING` command over connection  |
| 2    | Server | Validates the command                 |
| 3    | Server | Responds with `PONG`                  |
| 4    | Client | Receives `PONG` and confirms liveness |

## Alternate Flows

### A1: Invalid Command Format

* Server replies: `ERR wrong number of arguments for 'PING'`

### A2: Connection Lost

* Server does not respond
* Client times out and marks connection as broken

### A3: Server Overloaded

* Response is delayed or dropped
* Client retries or reconnects based on configuration

## Related Functional Requirements

* FR-1: Command Execution
* FR-2: Connection Handling
* FR-3: Health Check (PING/PONG)

## Related Nonfunctional Requirements

* NFR-2.1: Latency (PING round-trip ≤ 10ms under normal load)
* NFR-2.2: Availability (Server responds to PING during normal operation)
* NFR-2.3: Reliability (Client detects failure on missing response)

## Notes

* `PING` is often used as a heartbeat mechanism to keep idle connections alive
* Future versions may allow `PING <message>` returning `<message>` instead of `PONG` for debugging purposes

