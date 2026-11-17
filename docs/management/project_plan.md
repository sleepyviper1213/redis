# Software Project Management Plan (SPMP)

**Document ID:** SPMP-001 · **Revision:** 0.1 (stub) · **Date:** 17 Nov 2026
**Structure:** IEEE Std 1058 / ISO/IEC/IEEE 16326

Only the baseline lives here. Live schedule, status and effort are on the
GitHub Project board ([tracking_conventions.md](tracking_conventions.md)).

## 1. Overview
### 1.1 Purpose, scope, objectives
TODO: one paragraph, consistent with SRS §1.1–1.2.
### 1.2 Assumptions and constraints
TODO: single developer; Linux verified only through CI; graduation deadline.
### 1.3 Deliverables
TODO: source, SRS, SDD, test report, benchmark report, user guide, thesis.
### 1.4 Evolution of this plan
Revised at each milestone; revision table below.

## 2. References
SRS-001, SDD-001, TEST-001, [docs/README.md](../README.md).

## 3. Definitions
See SRS §1.3.

## 4. Project organisation
### 4.1 Process model
Iterative, two-week iterations (Scrum-like), single developer.
### 4.2 Organisational structure and responsibilities
| Role | Person |
|------|--------|
| Developer / project manager | Khac Truong Nguyen |
| Supervisor | TODO |

### 4.3 Tools and techniques
Git, GitHub Projects and Actions, CMake and vcpkg, PlantUML, redis-benchmark. TODO: complete.

## 5. Management process
### 5.1 Objectives and priorities
TODO.
### 5.2 Assumptions, dependencies, constraints
TODO.
### 5.3 Risk management
[risk_management_plan.md](risk_management_plan.md).
### 5.4 Monitoring and control
Board Insights per iteration; milestone export (see tracking conventions); supervisor review at each milestone.
### 5.5 Staffing
One developer.

## 6. Technical process
### 6.1 Methods, tools, techniques
[ADRs](../design/adr); coding standard (TODO).
### 6.2 Software documentation
[Documentation map](../README.md).
### 6.3 Project support functions
Configuration management, quality assurance, V&V: TODO plans under `docs/quality/`.

## 7. Work packages, schedule, budget
### 7.1 Work packages
Epics on the board, one per SRS feature section.
### 7.2 Milestones (baseline dates)
| Milestone | Planned date |
|-----------|--------------|
| M1 Requirements baseline | TODO |
| M2 Design baseline | TODO |
| M3 Feature complete | TODO |
| M4 Test and evaluation complete | TODO |
| M5 Thesis submission | TODO |

### 7.3 Budget
No monetary budget. Effort estimates in the Estimate field.

## Revision history
| Date | Revision | Author | Change |
|------|----------|--------|--------|
| 17 Nov 2026 | 0.1 | Khac Truong Nguyen | Stub |
