# ADR-0010: Pinned vcpkg baseline and layered CMake presets

- **Status:** Accepted (backfilled)
- **Date:** 2025-09-01 (vcpkg in CI, `3fe0e04`); 2026-10-28 (presets, `27c32e4`); 2026-11-16 (pins, `cc72abc`); 2026-11-17 (naming, `c567bec`)
- **Requirements:** NFR-007, NFR-006
- **Source:** SRS risk R-002

## Context

The project builds with GCC, Clang, MSVC/clang-cl and MinGW on Linux, macOS
and Windows, and depends on young libraries (stdexec, reflect-cpp, abseil).
Unpinned dependencies broke builds repeatedly (`bbda439`, `ad89f97`, `5b7ee9e`).

## Decision

- Pin the vcpkg baseline and every direct dependency, and reflect-cpp's, so a shallow clone builds (`cc72abc`).
- Layer the CMake presets: base, options, toolchain, platform (`cmake/presets/`), named `arch-os-abi-compiler-variant`.
- CI builds every preset, with each sanitizer as a configuration of its OS's main tree (`cf573c3`, `0b0cfbd`).

## Alternatives considered

- **Floating baseline:** newest fixes, but builds break without a change in this repo.

## Consequences

- Reproducible builds; sanitizers and analysis are one preset away.
- Upgrading a dependency is a deliberate change, and the CI matrix is large and slow.
