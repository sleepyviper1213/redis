# Changelog

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versioning: [SemVer](https://semver.org/).

## [Unreleased]

### Added
- Key expiry specification and test scenarios (SRS rev 6).
- Documentation index, ADRs 0001–0010, project plan, risk plan and traceability stubs.

### Changed
- Session round loop is a coroutine; accept loop takes an `AcceptSource` interface.
- Presets renamed `arch-os-abi-compiler-variant`.

<!-- TODO: backfill earlier releases from tags once the first is cut. -->
