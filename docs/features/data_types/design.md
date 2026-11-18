# Data Types: Design Choices

Choices specific to this feature. System-wide design stays in the
[SDD](../../design/software_design_document.md); a decision that was a real trade-off has an
[ADR](../../design/adr/). Rows are added when the feature changes.

| #       | Choice | Why | Where recorded |
|---------|--------|-----|----------------|
| DX-01 | Four value types (string, list, set, hash) held in one `Object` that knows its type | A key holds exactly one type; `TYPE` and snapshots read the tag (from code) | SDD §6.3, FR-001 to FR-004; `src/common/store/detail/basic_object.hpp` |
| DX-02 | Typed access through `find_as<T>` (missing key gives `nullptr`, wrong type gives a `WrongType` failure) and `find_or_create<T>` on first write | A command never sees a value of the wrong type | SDD §6.3 |
| DX-03 | An emptied list, set or hash is removed from the keyspace by the command that emptied it | Matches Redis (`erase_if_empty` in the commands, from code) | SDD §6.3, UC-004 notes |
| DX-04 | Arguments are `string_view`s into the session's read buffer; a command copies only what it stores | Fewer allocations on the request path; views dangle past the batch | [ADR-0004](adr/0004-zero-copy-request-arguments.md), SDD §6.4, D4 |
| DX-05 | One command runs at a time, so `INCR` and every multi-element command are atomic | No locks, no lost update | [ADR-0001](../connection/adr/0001-single-threaded-command-executor.md), UC-007 notes |
| DX-06 | Commands return `resp::Value`; a failure such as `WrongType` is a value turned into an error reply | No exceptions on the request path | [ADR-0009](../../design/adr/0009-errors-as-values.md), SDD §14.5 |
| DX-07 | A list is a ring buffer; a set and a hash are hash tables with a transparent string hash; strings are an sds-style string | (from code) `basic_value_types.hpp` | SDD §6.1 (lookups by `string_view`) |
| DX-08 | Set replies (`SMEMBERS`, `SINTER`) have no defined order; `SPOP` is logged as `SREM` of the members taken | A replay must not pop other members | UC-010 notes, SDD §8.2 |
| DX-09 | Expiry applies to the whole key, not to hash fields | (from code and UC-009 notes) | UC-009 notes |

## Code map

* `src/server/commands/` : `string.cpp`, `list.cpp`, `hash.cpp`, `set.cpp` (one command table per type), `handler.cpp`, `args.hpp`, `command.hpp`.
* `src/common/store/` : `database.hpp`, `object.hpp`, `simple_dynamic_string.hpp`, and `detail/basic_database.hpp`, `detail/basic_object.hpp`, `detail/basic_value_types.hpp`, `detail/object_type.hpp`.
* `src/core/utils/` : `ring_buffer.hpp`, `transparent_hash.hpp`, `parse_number.hpp` (counters).

## Open questions

* Blocking pops (`BLPOP`) are not supported ([UC-008 notes](use_cases/uc_008_use_lists.md)); TODO.md notes pops would be at-most-once if they are added.
