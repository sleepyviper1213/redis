# Features

Stable documents (SRS, SDD, master test plan) change rarely. Everything about one feature that is
likely to change together lives in one folder here, so a change touches one place.

| Feature | Folder | Requirements | Status |
|---------|--------|--------------|--------|
| Data types | [data_types/](data_types/) | FR-001 to FR-004 | Implemented |
| Keyspace | [keyspace/](keyspace/) | FR-005 | Implemented |
| Key expiry | [key_expiry/](key_expiry/) | FR-006.x, NFR-010 | Implemented; full folder |
| Persistence | [persistence/](persistence/) | FR-011, FR-012 | Implemented |
| Connection | [connection/](connection/) | FR-008, FR-009, FR-014 | Implemented |
| Observability | [observability/](observability/) | FR-007, FR-015, FR-016 | Implemented |
| Server lifecycle | [server_lifecycle/](server_lifecycle/) | FR-013, FR-017, FR-018 | Implemented |
| Extensibility | [extensibility/](extensibility/) | BR-009, FR-020 to FR-025 | Planned |

Only key expiry has the full set of documents. The others have a README and their use cases, and gain the rest
from [_template/](_template/) the next time they change.

## Layout of a feature folder

```text
docs/features/<feature>/
├── README.md         Summary, status, requirement IDs, use case list, known limits
├── use_cases/        Use cases for this feature, with acceptance criteria AC-nn
├── design.md         Design choices (DX-nn); links to ADRs and SDD sections
├── adr/              Architecture decisions that belong to this feature (optional)
├── test_plan.md      Test scenarios TC-nnn, levels, timing rules
├── traceability.md   AC <-> use case <-> requirement <-> test; NFR verification
└── diagrams/         PlantUML for this feature only
```

System-wide diagrams (components, deployment, the data flow diagrams, the use case overview and the command execution
pipeline) stay in [analysis/diagrams/](../analysis/diagrams/). A diagram moves into a feature folder when it shows only that
feature.

Start a new one by copying [_template/](_template/).

## ID ranges

IDs are serial numbers for the whole project, never reused and never renumbered, so an ID cited in an issue, a PR or
another document means one thing. Take a new ID with `scripts/next_id.sh TC` (or `DX`, `LI`, `AC`; a count as second
argument), then add it to the feature's row below. The script keeps its counters in `.ids.local` at the root of the
main checkout, gitignored and shared by every worktree, and also scans the docs of every branch and checkout, so
branches worked on side by side never take the same number and never edit a shared "next free" line.

| Feature | Scenarios `TC-nnn` | Limits by design `LI-nn` (issues) | Design choices `DX-nn` | Criteria `AC-nn` |
|---------|--------------------|----------------------|------------------------|------------------|
| (master test plan, integration) | TC-001 to TC-027 | | | |
| data_types | TC-028 to TC-064 | LI-01 | DX-01 to DX-09 | |
| keyspace | TC-065 to TC-085 | LI-02, LI-03; #40 | DX-10 to DX-16 | |
| key_expiry | TC-086 to TC-123, TC-263 to TC-270, TC-322 to TC-328 | LI-05; #40 to #42, #45, #48, #49, #53, #54 | DX-17 to DX-23, DX-79, DX-90, DX-91 | AC-01 to AC-11 |
| persistence | TC-124 to TC-159, TC-261, TC-297 | | DX-24 to DX-33, DX-77 | |
| connection | TC-160 to TC-192, TC-271, TC-278 to TC-294, TC-298 to TC-300, TC-307, TC-308, TC-320, TC-321 | | DX-34 to DX-44, DX-81, DX-86, DX-89 | |
| observability | TC-193 to TC-226, TC-272 to TC-277, TC-301 to TC-306 | | DX-45 to DX-55, DX-80, DX-84, DX-85 | |
| server_lifecycle | TC-227 to TC-252, TC-262, TC-295, TC-296, TC-334 to TC-337 | | DX-56 to DX-65, DX-76, DX-78, DX-82, DX-94, DX-95 | |
| extensibility | TC-253 to TC-260, TC-309 to TC-319 | | DX-66 to DX-75, DX-87, DX-88 | |

Legacy scenario IDs outside these ranges keep their numbers and are not
renumbered: `TC-1001` to `TC-1004` (ping and echo, in `connection`) and `TC-2001`, `TC-2002` (RESP encoding and
decoding, in `connection`). Sub-scenarios keep a letter suffix (`TC-104a`). Use cases keep their
own numbering (`UC-nnn`).

## Issues

GitHub numbers every issue (`#40`), and that number is the tracker ID: titles carry no prefix of their own. Labels
say what an issue is and where it belongs.

| Label | Meaning |
|-------|---------|
| `bug`, `enhancement`, `task`, `spike`, `risk`, `epic`, `documentation` | The type. One per issue |
| `feat:<folder>` | The feature, `<folder>` being a folder here (`feat:key_expiry`). An issue may have more than one |

The issue body names its requirement: `**Requirement:** FR-006.8` under "Additional context" in the bug and feature
templates, or a `## Requirement` heading in the task template. The [issue index workflow](../../.github/workflows/issue_index.yml) runs
[`scripts/issue_index.py`](../../scripts/issue_index.py) when an issue changes, and daily. It rewrites
`<feature>/issues.md` from the issues carrying that feature's label, and opens or updates one pull request on the branch
`bot/issue-index`. Nothing is pushed to the default branch, and `issues.md` is never edited by hand.

Documents cite an issue by number. A limit that is by design keeps an ID `LI-nn`, the next free one below. A bug or a
missing feature is an issue, and `LI-nn` is not used for it. A bug's test states the correct behaviour and is tagged
`[!shouldfail]` until the fix; a missing feature's test pins today's reply. Retired for that reason, not
reused: LI-04, LI-06, LI-07, LI-08 (now #40, #40, #41, #42).

## What stays outside the folder

| Document | Why |
|----------|-----|
| Requirements (`FR-*`, `NFR-*`) in the [SRS](../analysis/software_requirement_specification.md) | The single source; features reference IDs and never restate them |
| System-wide design in the [SDD](../design/software_design_document.md) and [ADRs](../design/adr/) | A decision with a real trade-off gets an ADR: in the feature's `adr/` if it belongs to one feature, in `design/adr/` if it spans features; the feature's `design.md` links to it |
| Use case index and actors | [analysis/use_cases/README.md](../analysis/use_cases/README.md) lists every use case and links to its feature folder; the files themselves live in `features/<feature>/use_cases/`. A use case serving two features (for example `SET`) lives in the one that owns the command; the other links to it |
| Test levels, environments, results | [Master test plan](../test/test_plan_and_report.md) |
| Project-wide matrix | [quality/traceability_matrix.md](../quality/traceability_matrix.md) gets one row per feature, pointing to the feature's `traceability.md` |

## Checklist for a new feature or a change

1. Issue with its requirement ID.
2. Requirements in the SRS (if new).
3. Folder from the template; use cases and acceptance criteria.
4. Design choices; ADR if it was a real trade-off.
5. Test scenarios and traceability, **before** code changes.
6. Update the feature README status and the project matrix row.
