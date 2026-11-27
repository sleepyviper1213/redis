#!/usr/bin/env bash
# Fail if code outside the platform layer branches on the platform
# (docs/design/patterns.md, "Platform code"). Everything else must build and
# be tested on every platform, so CI only has to cover src/core/os/ and the
# crash handler on Linux and macOS.
#
#     scripts/check_platform_code.sh
set -euo pipefail
cd "$(dirname "$0")/.."

pattern='^[[:space:]]*#[[:space:]]*(if|ifdef|ifndef|elif).*(_WIN32|_WIN64|__linux__|__APPLE__|__unix__|__FreeBSD__)'
found=$(grep -rnE "$pattern" src --include='*.cpp' --include='*.hpp' --include='*.tpp' \
    | grep -vE '^src/core/os/|^src/server/diagnostics/' || true)

if [ -n "$found" ]; then
    echo "Platform branches outside src/core/os/ and src/server/diagnostics/:" >&2
    echo "$found" >&2
    echo "Move them behind an interface in src/core/os/ (docs/design/patterns.md)." >&2
    exit 1
fi
echo "platform code: only in src/core/os/ and src/server/diagnostics/"
