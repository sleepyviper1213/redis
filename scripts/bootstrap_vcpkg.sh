#!/usr/bin/env bash
# Clone vcpkg at the commit pinned by "builtin-baseline" in vcpkg.json and
# bootstrap it into $VCPKG_ROOT. For CI.
#
# A plain `git clone --depth 1` takes today's vcpkg, whose ports (stdexec, ...)
# can differ from the ones the code was written against, and a shallow clone
# cannot resolve a baseline at all: vcpkg reads it from the commit's own
# versions/ database. So fetch exactly that commit.
set -euo pipefail
cd "$(dirname "$0")/.."

: "${VCPKG_ROOT:?VCPKG_ROOT must name the directory to clone into}"

baseline="$(sed -n 's/.*"builtin-baseline": *"\([0-9a-f]\{40\}\)".*/\1/p' vcpkg.json)"
[[ -n "$baseline" ]] ||
    { echo "error: no builtin-baseline in vcpkg.json" >&2; exit 1; }

git init -q "$VCPKG_ROOT"
git -C "$VCPKG_ROOT" fetch -q --depth 1 https://github.com/microsoft/vcpkg "$baseline"
git -C "$VCPKG_ROOT" checkout -q FETCH_HEAD
"$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
