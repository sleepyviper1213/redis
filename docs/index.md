# Project Documentation Index

This documentation provides an internal overview of the Redis-like in-memory database server, intended for academic review, maintenance, and technical planning. It complements source code with analysis, design, testing, and use case artifacts.

---

## Document Structure

### 1. Analysis

- [`use_cases/set.md`](use_cases/set.md) — `SET` command use case
- [`use_cases/get.md`](use_cases/get.md) — `GET` command use case
- [`use_cases/del.md`](use_cases/del.md) — `DEL` command use case
- [`use_cases/save.md`](use_cases/save.md) — `SAVE` snapshot use case

Each use case includes:
- Actors
- Pre/Post Conditions
- Main & Alternate Flows
- Linked Sequence Diagrams

---

### 2. 🏗️ Design

- [`design/architecture.md`](design/details.md) — system architecture
- [`design/persistence.md`](design/persistence.md) — snapshot & serialization
- [`design/network.md`](design/network.md) — HTTP & request handling

Associated diagrams:
- [`diagrams/class_diagram_store.puml`](diagrams/class_diagram_store.puml)
- [`diagrams/sequence_set_command.mmd`](diagrams/sequence_set_command.mmd)

---

### 3. Testing

- Unit test source: [`../tests/`](../tests/)
- Test logs: [`../test_logs/`](../test_logs/)
- CI reports: GitHub Actions > Workflows tab

Instructions and expectations are described in:
- [`testing/plan.md`](testing/plan.md)
- [`testing/coverage.md`](testing/coverage.md) *(if coverage is implemented)*

---

### 4. Implementation Traceability

- Issues and development tasks tracked in GitHub Projects board.
- Each deliverable link to:
    - A GitHub Issue
    - A corresponding branch (`feature/usecase-set`, etc.)
    - An implementation pull request
    - Related design/test files in this `docs/` folder

---

### 5. References

- Redis official documentation: [https://redis.io](https://redis.io)
- University grading rubric and course guidelines
- External libraries: Boost.Asio, spdlog, cereal, etc.

---

_Last updated: July 2025_
