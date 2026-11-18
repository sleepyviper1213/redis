# Documentation Index

Single entry point for all project documents. Organised by lifecycle process
(ISO/IEC/IEEE 12207) with one IEEE standard per document type.
Split of responsibility:

- **GitHub Projects = fast-changing data:** status, iterations/sprints, assignees,
  priority, size/estimates, burndown, roadmap dates, retros (Discussions).
  Never duplicated in `docs/`.
- **`docs/` = slow-changing, reviewable data:** requirements, design, decisions,
  plans, baselines and policies. Changes go through PRs.

Test: if it changes more than once per sprint, it belongs in Projects.

## Jira → GitHub mapping

| Jira concept        | GitHub Projects / repo equivalent                              | Durable copy in `docs/`            |
|---------------------|----------------------------------------------------------------|------------------------------------|
| Project / Roadmap   | Project (v2) Roadmap view (live dates)                         | `management/project_plan.md` (scope, milestones, baseline only) |
| Epic                | Issue with label `epic` + sub-issues                           | none; epics map to SRS feature sections |
| Task / Bug / Feature| Issue (templates in `.github/ISSUE_TEMPLATE/`)                 | Requirement IDs in the SRS         |
| Sprint              | Iteration field                                                | none                               |
| Board / Status      | Status field (Backlog, Ready, In progress, In review, Done)    | none                               |
| Estimates / Effort  | Size / Estimate fields                                         | none                               |
| Retrospective       | GitHub Discussions (category "Retro")                          | none                               |
| Version / Release   | Milestone + Release tag                                        | `CHANGELOG.md`, `release/`         |
| Workflow            | Branch + PR + CI                                               | `quality/configuration_management_plan.md` |
| Confluence          | `docs/`                                                        | this tree                          |
| Dashboards / Reports| Insights charts                                                | none                               |

Rule: every issue references a requirement ID (`FR-xxx`/`NFR-xxx`), and every PR
references an issue (`Closes #n`). That chain is the traceability matrix.

## Document map

Status: ✅ exists · 🟡 partial · ❌ missing

### Management (planning and control)

| Document                                   | Standard                    | Status | Path |
|--------------------------------------------|-----------------------------|--------|------|
| Software Project Management Plan (SPMP)    | IEEE 1058 / ISO 16326       | 🟡 stub | `management/project_plan.md` |
| Risk management plan (policy, scales, thresholds) | ISO 16085, IEEE 1540 | 🟡 stub | `management/risk_management_plan.md` |
| Meeting minutes (supervisor reviews, decisions only) | n/a          | ❌ | `management/meetings/` |
| Definition of Ready / Done                 | n/a (Scrum)                 | ✅ | `management/definition_of_done.md` |
| Tracking conventions (labels, fields, workflow, ID rules) | n/a      | ✅ | `management/tracking_conventions.md` |

Kept in GitHub Projects, not here: risk *register* (issues labelled `risk`,
with Probability/Impact fields), schedule, sprints, status, effort, retros.

### Quality and configuration

| Document                                   | Standard                    | Status | Path |
|--------------------------------------------|-----------------------------|--------|------|
| Software Quality Assurance Plan (SQAP)     | IEEE 730                    | ❌ | `quality/quality_assurance_plan.md` |
| Software Configuration Management Plan     | IEEE 828                    | ❌ | `quality/configuration_management_plan.md` |
| Verification and Validation Plan           | IEEE 1012                   | ❌ | `quality/vv_plan.md` |
| Coding standard / style guide              | n/a                         | 🟡 (.clang-format?, memory rules) | `quality/coding_standard.md` |
| Review and inspection records              | IEEE 1028                   | ❌ | `quality/reviews/` |
| Defect log and root-cause summaries        | IEEE 1044                   | 🟡 (GitHub issues) | `quality/defects.md` |
| Requirements Traceability Matrix (RTM)     | IEEE 29148                  | 🟡 seeded from SDD §16 | `quality/traceability_matrix.md` |

### Features (one folder each; changes together)

See [features/README.md](features/README.md): use cases with acceptance criteria, design choices, test plan and
traceability for one feature, copied from [features/_template/](features/_template/).

### Analysis (requirements)

| Document                                   | Standard                    | Status | Path |
|--------------------------------------------|-----------------------------|--------|------|
| Software Requirements Specification        | IEEE 830 / 29148            | ✅ | `analysis/software_requirement_specification.md` |
| Use cases UC-001…UC-022 (index in `analysis/use_cases/`, files in each feature folder) | Cockburn | ✅ | `features/<feature>/use_cases/` |
| Per-feature folders (use cases, design, test plan, traceability) | IEEE 29148 / 829 | ✅ key expiry | `features/<feature>/` |
| Diagrams (DFD, state, sequence, activity)  | UML / PlantUML              | ✅ | `analysis/diagrams/` (system-wide), `features/<feature>/diagrams/` |
| Glossary                                   | IEEE 24765                  | 🟡 (SRS §1.3) | keep in SRS |
| Stakeholder / StRS                         | ISO 29148 §9                | ❌ optional | `analysis/stakeholder_requirements.md` |

### Design

| Document                                   | Standard                    | Status | Path |
|--------------------------------------------|-----------------------------|--------|------|
| Software Design Description                | IEEE 1016                   | ✅ | `design/software_design_document.md` |
| Architecture Decision Records              | MADR / Nygard               | ✅ 10 backfilled | `features/<feature>/adr/` (feature), `design/adr/` (project-wide, and the index) |
| Directory layout                           | n/a                         | ✅ | `directory_layout.md` |
| Interface spec (RESP subset, command table)| IEEE 1016 §10               | 🟡 (inside SDD) | `design/interface_specification.md` |
| Persistence file-format spec (RDB/AOF)     | n/a                         | 🟡 | `design/file_formats.md` |
| Architecture views (C4 / 4+1)              | ISO 42010                   | 🟡 (components, deployment) | `design/` |

### Test and evaluation

| Document                                   | Standard                    | Status | Path |
|--------------------------------------------|-----------------------------|--------|------|
| Master test plan + level test report (strategy, results, anomalies) | IEEE 829 | ✅ | `test/test_plan_and_report.md` |
| Test scenarios (key expiry)                | IEEE 829 (test design/case) | ✅ | `features/key_expiry/test_plan.md` |
| Benchmark report                           | n/a                         | ✅ | `evaluation/benchmark_report.md` |
| Compatibility matrix vs. real Redis        | n/a                         | ❌ | `evaluation/compatibility_matrix.md` |
| Code coverage / sanitizer report           | IEEE 1012                   | ❌ (CI artefact) | `evaluation/quality_metrics.md` |
| User acceptance test record                | IEEE 829                    | ❌ | `test/acceptance_test_report.md` |

### Release and operation

| Document                                   | Standard                    | Status | Path |
|--------------------------------------------|-----------------------------|--------|------|
| User guide                                 | IEEE 26514                  | ✅ | `user/user_guide.md` |
| Build / install / deployment guide         | IEEE 26514                  | 🟡 (README) | `user/installation_guide.md` |
| Configuration reference (`config.toml`)    | IEEE 26514                  | 🟡 | `user/configuration_reference.md` |
| Changelog                                  | Keep a Changelog            | 🟡 stub | `/CHANGELOG.md` |
| Release notes + release checklist          | IEEE 12207 §6.4.10          | ❌ | `release/` |
| Known issues and limitations               | n/a                         | ❌ | `release/known_issues.md` |
| Software Version Description               | IEEE 12207 (SVD)            | ❌ optional | `release/version_description.md` |

### Repository root (GitHub community files)

| File                         | Status |
|------------------------------|--------|
| `README.md`                  | ✅ |
| `LICENSE`                    | ✅ |
| `CONTRIBUTING.md` (workflow, branching, commit style) | ✅ |
| `SECURITY.md`                | ❌ |
| `CODE_OF_CONDUCT.md`         | ❌ optional |
| `.github/pull_request_template.md` (Closes #, req ID, test evidence) | ✅ |
| `.github/CODEOWNERS`         | ❌ optional |
| `.github/ISSUE_TEMPLATE/` task, epic, spike templates (only bug + feature exist) | ✅ task, epic, spike, risk added |
| `TODO.md`                    | migrate each item to an issue, then delete |

## Conventions

- Each document has: ID, revision table, standard followed (see SDD/TEST headers).
- IDs: `SRS-001`, `SDD-001`, `TEST-001`, `SPMP-001`, `ADR-NNNN`, `RISK-NN`.
- Diagrams as PlantUML text; never commit only a PNG.
- A document is *baselined* by git tag (`docs-v1.0`); later edits bump revision.
- Live project data stays in GitHub Projects. For the thesis, take a dated
  screenshot or CSV export of the board at each milestone and attach it to
  the milestone's release notes, not to `docs/management/`.

## Suggested priority

1. **Now (cheap, high value for a thesis):** SPMP, risk management plan, RTM, ADRs
   (backfill the 5–8 big decisions: coroutine sessions, stdexec, snapshot format…),
   `CONTRIBUTING.md`, PR template, `CHANGELOG.md`.
2. **Before defence:** SQAP, SCMP, V&V plan, compatibility matrix, installation guide, known issues.
3. **Optional:** StRS, SVD, code of conduct.
