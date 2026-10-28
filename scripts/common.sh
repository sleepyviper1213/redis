#!/usr/bin/env bash
# Shared helpers for the scripts in this directory. Source, don't execute.

# locate_binary <configure-preset> <configuration> <target>
#
# Multi-config generators put binaries under build/<preset>/<subdir>/<Config>/.
locate_binary() {
    local preset="$1" config="$2" target="$3" candidate
    for candidate in \
        "build/$preset/src/server/$config/$target" \
        "build/$preset/test/$config/$target"; do
        if [ -x "$candidate" ]; then
            echo "$candidate"
            return 0
        fi
    done
    echo "error: $target not found for preset '$preset' ($config)." >&2
    # tr rather than ${config,,}: macOS ships bash 3.2.
    echo "       Build it first: cmake --build --preset $preset-$(echo "$config" | tr '[:upper:]' '[:lower:]')" >&2
    return 1
}
