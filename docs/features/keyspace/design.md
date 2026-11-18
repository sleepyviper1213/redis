# Keyspace: Design Choices

Choices specific to this feature. System-wide design stays in the
[SDD](../../design/software_design_document.md); a decision that was a real trade-off has an
[ADR](../../design/adr/). Rows are added when the feature changes.

| #       | Choice | Why | Where recorded |
|---------|--------|-----|----------------|
| DX-10 | One keyspace map, picked at configure time with `-DREDIS_KEYSPACE_MAP=absl`, `boost` or `std`; default `absl::flat_hash_map` | Open addressing, good cache behaviour | SDD §6.1 |
| DX-11 | Keys are owned strings; lookups take `string_view` through a transparent hash | A command never builds a key string to look one up | SDD §6.1 |
| DX-12 | One thread touches the keyspace; no locks | Each command is atomic; `DEL`, `KEYS`, `DBSIZE` never race a writer | [ADR-0001](../connection/adr/0001-single-threaded-command-executor.md), SDD D1 |
| DX-13 | `DEL` and `FLUSHDB` are idempotent; deleting a missing key is not an error | Matches Redis | UC-004 notes, FR-005 |
| DX-14 | A key whose last element is removed is deleted too | Matches Redis | SDD §6.3, UC-004 notes |
| DX-15 | `KEYS` uses a glob matcher (`*`, `?`, `[abc]`, `[^a]`, ranges, `\`) and visits every key, blocking other clients meanwhile | Redis syntax; accepted cost, reported by the command watchdog | SDD §13.5, §17, UC-012 notes |
| DX-16 | `TYPE` and `WRONGTYPE` both read the one type tag on the object | (from code) `find_as<T>` | SDD §6.3 |

## Code map

* `src/server/commands/keyspace.cpp` : `DEL`, `EXISTS`, `KEYS`, `TYPE`, `FLUSHDB`, `DBSIZE` (the expiry commands live there too; see [key expiry](../key_expiry/design.md)).
* `src/common/store/` : `database.hpp`, `detail/basic_database.hpp`, `detail/basic_value_types.hpp` (map selection).
* `src/core/utils/glob.hpp`, `src/core/utils/glob.cpp` : the pattern matcher.

## Open questions

* `SCAN` family as the incremental alternative to `KEYS`; document `KEYS` as a debugging command (TODO.md, "Slow commands and resource limits").
* Lazy freeing (`UNLINK`, async `FLUSHALL`) so freeing a huge value does not block the executor (TODO.md).
* Compare the three keyspace maps by benchmark (TODO.md).
* A slow command such as `KEYS` on a large keyspace delays every client (SDD §17).
