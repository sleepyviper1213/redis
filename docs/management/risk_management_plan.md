# Risk Management Plan

**Document ID:** RMP-001 · **Revision:** 0.1 (stub) · **Date:** 17 Nov 2026
**Structure:** ISO/IEC/IEEE 16085

## 1. Purpose

Define how risks are identified, scored, tracked and closed. The live register
is the set of GitHub issues of type *Risk* (fields Probability, Impact); this
document holds the policy. The initial risks R-001 to R-009 are in
[SRS §2.6](../analysis/software_requirement_specification.md); each is to be
filed as a Risk issue.

## 2. Scales

| Level  | Probability                | Impact                                   |
|--------|----------------------------|------------------------------------------|
| Low    | unlikely before submission | workaround exists, no milestone slips    |
| Medium | plausible                  | one milestone slips or scope is cut      |
| High   | expected                   | data loss, or the thesis goal is missed  |

Exposure = Probability × Impact (Low=1, Medium=2, High=3). 6 or more needs a mitigation plan and a weekly look; 9 escalates to the supervisor.

## 3. Process

1. **Identify:** at each milestone review, after a failed CI trend, after a dependency change.
2. **Analyse:** set Probability and Impact; write trigger, mitigation and contingency in the issue.
3. **Treat:** avoid, mitigate, transfer or accept; link the issue that carries out the mitigation.
4. **Monitor:** review the open Risk issues at each iteration start.
5. **Close:** when the trigger can no longer happen; say why.

## 4. TODO

- [x] Migrate SRS risks R-001 to R-009 into issues (17 Nov 2026). Probability, Impact and Trigger/Contingency still to set.
- [ ] Add project-level risks: schedule, dependency instability (stdexec), single-person bus factor, Linux testing only through CI.
