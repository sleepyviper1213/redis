#!/usr/bin/env bash
# Hand out the next free project ID (docs/features/README.md#id-ranges).
#
#     scripts/next_id.sh TC        # one scenario ID, e.g. TC-322
#     scripts/next_id.sh DX 2      # two design-choice IDs
#
# Kinds: TC (scenarios), DX (design choices), LI (limits by design), AC
# (acceptance criteria). IDs are never reused, but branches are worked on side by
# side, so a "next free" line in a tracked file would be edited by every branch
# and two branches would take the same number. Instead the counters live in
# .ids.local at the root of the main checkout: gitignored, and shared by every
# worktree of the clone. Each call also scans the docs of every local branch,
# every remote-tracking branch and every checkout (uncommitted work included), so
# a missing or stale counter file is harmless.
set -euo pipefail

usage() { echo "usage: scripts/next_id.sh TC|DX|LI|AC [count]" >&2; exit 2; }

kind="${1:-}"
count="${2:-1}"
case "$kind" in TC|DX|LI|AC) ;; *) usage ;; esac
case "$count" in '' | *[!0-9]* | 0) usage ;; esac

common_dir="$(git rev-parse --path-format=absolute --git-common-dir)"
counter_file="$(dirname "$common_dir")/.ids.local"
lock="$counter_file.lock"

# Two sessions asking at once must not get the same number.
for _ in $(seq 100); do
    if mkdir "$lock" 2>/dev/null; then break; fi
    sleep 0.1
done
[ -d "$lock" ] || { echo "error: $lock is held; remove it if no other call is running" >&2; exit 1; }
trap 'rmdir "$lock"' EXIT

# Largest ID of this kind in use anywhere. Lines that only announce a "next
# free" number (the table's old footer, kept in older branches) are skipped:
# that number was never used. TC numbers of four digits or more are the legacy
# TC-1001 and TC-2001 series, outside the sequence.
highest_in_use() {
    {
        local refs
        refs="$(git for-each-ref --format='%(objectname)' refs/heads refs/remotes | sort -u)"
        if [ -n "$refs" ]; then
            # shellcheck disable=SC2086
            git grep -h -E "\\b$kind-[0-9]+" $refs -- docs 2>/dev/null || true
        fi
        git worktree list --porcelain | sed -n 's/^worktree //p' | while read -r tree; do
            [ -d "$tree/docs" ] && grep -r -h -E "\\b$kind-[0-9]+" "$tree/docs" 2>/dev/null || true
        done
    } | grep -v -i 'next free' | grep -o -E "\\b$kind-[0-9]+" |
        sed -E "s/^$kind-0*([0-9]+)$/\\1/" |
        awk -v kind="$kind" '!(kind == "TC" && $1 >= 1000) && $1 > max { max = $1 } END { print max + 0 }'
}

stored=0
if [ -f "$counter_file" ]; then
    stored="$(awk -v kind="$kind" '$1 == kind { print $2 + 0 }' "$counter_file")"
    stored="${stored:-0}"
fi
scanned=$(( $(highest_in_use) + 1 ))
next=$(( stored > scanned ? stored : scanned ))

width=2
[ "$kind" = TC ] && width=3
for (( i = 0; i < count; ++i )); do
    printf "%s-%0${width}d\n" "$kind" $(( next + i ))
done

# Record the new next value, keeping the other kinds' lines.
{
    [ -f "$counter_file" ] && awk -v kind="$kind" '$1 != kind' "$counter_file"
    echo "$kind $(( next + count ))"
} > "$counter_file.tmp"
mv "$counter_file.tmp" "$counter_file"
