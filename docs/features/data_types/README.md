# Feature: Data types

| Item         | Value |
|--------------|-------|
| Feature ID   | FEAT-DATA_TYPES |
| Status       | Implemented; documented retroactively |
| Requirements | FR-001 to FR-004 (defined in the [SRS](../../analysis/software_requirement_specification.md); not repeated here) |
| Issue / epic | TODO |

Strings, lists, hashes and sets: the typed values a client stores and reads.

## Use cases

| Use case | Actor | Commands / trigger | Requirements |
|----------|-------|--------------------|--------------|
| [UC-002 Set key-value pair](use_cases/uc_002_set_key_value.md) | Client | `SET` | FR-001, FR-006 |
| [UC-003 Get key value](use_cases/uc_003_get_key_value.md) | Client | `GET` | FR-001 |
| [UC-007 Update strings and counters](use_cases/uc_007_update_strings_and_counters.md) | Client | `MSET`, `MGET`, `APPEND`, `STRLEN`, `INCR`, `DECR`, `INCRBY`, `DECRBY` | FR-001 |
| [UC-008 Store and read lists](use_cases/uc_008_use_lists.md) | Client | `LPUSH`, `RPUSH`, `LPOP`, `RPOP`, `LLEN`, `LRANGE`, `LINDEX` | FR-002 |
| [UC-009 Store and read hashes](use_cases/uc_009_use_hashes.md) | Client | `HSET`, `HGET`, `HMGET`, `HDEL`, `HGETALL`, `HLEN`, `HEXISTS` | FR-003 |
| [UC-010 Store and read sets](use_cases/uc_010_use_sets.md) | Client | `SADD`, `SREM`, `SCARD`, `SMEMBERS`, `SISMEMBER`, `SINTER`, `SPOP` | FR-004 |

## Diagrams

* Typed lookup, `WRONGTYPE`, creation on first write and removal of an emptied container: [activity_typed_access.puml](diagrams/activity_typed_access.puml)

## Documents in this folder

| Document | Content |
|----------|---------|
| [test_plan.md](test_plan.md) | Test scenarios `TC-nn` and their coverage by existing tests |
| [traceability.md](traceability.md) | Use case to requirement, scenario and test |
| [design.md](design.md) | Design choices `DX-n`, code map, open questions |
| [adr/](adr/) | Decisions: ADR-0004 |
| [diagrams/](diagrams/) | PlantUML |
