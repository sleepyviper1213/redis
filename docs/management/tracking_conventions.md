# Tracking Conventions

**Document ID:** TRK-001 · **Revision:** 1.0 · **Date:** 17 Nov 2026

How work is tracked in GitHub Projects ("Redis", user project #7) and how it
links back to `docs/`. Live data stays on the board; see [docs/README.md](../README.md).

## Workflow

`Backlog → Ready → In progress → In review → Done`

- **Ready:** meets the Definition of Ready in [definition_of_done.md](definition_of_done.md).
- **In review:** a PR is open and linked (`Closes #n`).
- **Done:** merged, CI green, Definition of Done met.

## Fields

| Field        | Type            | Values / use                                  | State on board |
|--------------|-----------------|-----------------------------------------------|----------------|
| Status       | single select   | as above                                      | exists |
| Priority     | single select   | P0 blocker, P1 must, P2 should, P3 could      | **add P3** |
| Size         | single select   | XS, S, M, L, XL                               | exists |
| Estimate     | number          | optional hours                                | exists |
| Iteration    | iteration       | 14 days, Monday start                         | **stale: replace** |
| Type         | issue type / label | Epic, Feature, Task, Bug, Spike, Risk      | **create** |
| Probability  | single select   | Low, Medium, High (Risk only)                 | **create** |
| Impact       | single select   | Low, Medium, High (Risk only)                 | **create** |
| Requirement  | text            | `FR-006.3`, `NFR-001` ... comma separated     | **create** |
| Milestone    | built-in        | M1 to M5 below                                | **create** |

## Manual setup checklist (tools cannot do this; use the web UI)

1. Project → Settings → Priority: add option `P3`.
2. Project → Settings → Iteration: replace the three 2025 iterations with 14-day iterations from **2026-11-23**, about six.
3. Add single-select fields `Probability` and `Impact` (Low, Medium, High) and a text field `Requirement`.
4. Type: use issue types if the repo offers them, otherwise labels `epic`, `risk`, `spike`.
5. Repo → Issues → Labels: create `epic`, `risk`, `spike`.
6. Repo → Issues → Milestones: create the five below (no due dates yet).

## Milestones

| #  | Milestone                    | Exit evidence |
|----|------------------------------|---------------|
| M1 | Requirements baseline        | SRS reviewed, tag `docs-req-1.0` |
| M2 | Design baseline              | SDD and ADRs reviewed, tag `docs-design-1.0` |
| M3 | Feature complete             | All must-have FRs implemented |
| M4 | Test and evaluation complete | Test report and benchmark report final |
| M5 | Thesis submission            | Docs frozen, release tag |

## Rules

- **Every issue** names the requirement it serves in the `Requirement` field. Pure
  infrastructure work uses `NFR-006` or `NFR-007`.
- **Every PR** says `Closes #n` and lists test evidence ([PR template](../../.github/pull_request_template.md)).
- **Risks** are issues of type Risk with Probability, Impact and a mitigation
  in the body; policy is in [risk_management_plan.md](risk_management_plan.md).
- **Epics** are issues with sub-issues, one per SRS feature section.
- **Spikes** are time-boxed; the output is an ADR or a closed issue with the finding.
- **Decisions** that survive the issue go into an ADR ([design/adr](../design/adr)).
- **Milestone close:** export the board to CSV, attach it to the release notes.
