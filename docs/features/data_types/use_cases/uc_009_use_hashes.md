# Use Case: Store and Read Hashes

## Use Case ID

UC-009

## Description

Client keeps a record of field-value pairs under one key: it sets fields (`HSET`), reads them (`HGET`, `HMGET`,
`HGETALL`), checks and counts them (`HEXISTS`, `HLEN`), and removes them (`HDEL`).

## Primary Actor

Client (redis-cli or any Redis client)

## Preconditions

* Server is running
* Command syntax is valid:
  * `HSET <key> <field> <value> [<field> <value> ...]`
  * `HGET <key> <field>`, `HMGET <key> <field> [<field> ...]`, `HGETALL <key>`
  * `HEXISTS <key> <field>`, `HLEN <key>`, `HDEL <key> <field> [<field> ...]`

## Postconditions

* Set fields hold their new values; a missing key is created as a hash
* A hash whose last field is removed is deleted
* With the append-only file on, every write is logged

## Flow of Events

| Step | Actor  | Description                                                           |
|------|--------|-----------------------------------------------------------------------|
| 1    | Client | Sends `HSET user:1 name Ann age 31`                                   |
| 2    | Server | Parses the request and checks that fields and values come in pairs    |
| 3    | Server | Creates the hash if it is missing, sets each field                    |
| 4    | Server | Replies with the number of fields that were new, here `2`             |
| 5    | Client | Sends `HGET user:1 name`                                              |
| 6    | Server | Replies with the value as a bulk string, here `Ann`                   |

## Alternate Flows

### A1: Field or key missing

* `HGET` replies with a null bulk string; `HMGET` puts null in place of each missing field
* `HGETALL` on a missing key replies with an empty array; `HLEN` and `HEXISTS` reply `0`

### A2: Read the whole hash (`HGETALL`)

* Server replies with an array of field, value, field, value, ...; the order is not defined

### A3: Remove fields (`HDEL`)

* Server replies with the number of fields removed; missing fields are not counted

### A4: Key holds another type

* Server replies `WRONGTYPE Operation against a key holding the wrong kind of value`

### A5: Wrong number of arguments

* A field without a value in `HSET`, or too few arguments to any command, gets
  `ERR wrong number of arguments for '<command>' command`

## Related Functional Requirements

* FR-003: Hash commands
* FR-010: Error replies
* FR-012: Append-only file

## Related Nonfunctional Requirements

* NFR-001: Performance
* NFR-004: Compatibility

## Notes

* Expiry applies to the whole key, not to single fields.
