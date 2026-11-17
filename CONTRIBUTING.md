# Contributing

## Workflow

1. Pick an issue in the *Ready* column of the GitHub Project. No issue, no PR: open one first.
2. Branch from `main`: `feat/<topic>`, `fix/<topic>`, `docs/<topic>`.
   CI-only branches use the prefixes `test/`, `bench/` and `profile/` (see `.github/workflows`).
3. Commit with a short subject, no long body: `[type] scope: message`
   (`[feat]`, `[fix]`, `[refactor]`, `[perf]`, `[test]`, `[build]`, `[ci]`, `[docs]`, `[chore]`).
4. Open a PR that says `Closes #n`; fill in the template.
5. Merge when CI is green and the Definition of Done is met
   ([docs/management/definition_of_done.md](docs/management/definition_of_done.md)).

## Where things go

Start at [docs/README.md](docs/README.md). Fast-changing data (status, sprint,
estimates, risks) lives on the GitHub Project; decisions and baselines live in `docs/`.
Work on a feature lives in `docs/features/<feature>/` (copy `_template/`); update its use cases, test plan and
traceability before changing code. A significant design decision needs an ADR: in the feature's `adr/` folder, or in [docs/design/adr](docs/design/adr/) if it spans features. Number it from the index there.

## Building and testing

See [README.md](README.md) and [docs/directory_layout.md](docs/directory_layout.md).
