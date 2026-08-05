#!/usr/bin/env bash
# © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#
# Turn a classifier verdict into a per-lane decision. See issue #620.
#
# Usage:  lane-runs.sh <verdict> <lane-id>
# Output: "true" or "false" on stdout.
#
# THE CONTRACT — an unknown verdict or an unknown lane prints "true". Every path
# through this script that is not a deliberate, justified skip must run the lane.
# A typo in a lane id must cost compute, never coverage.
#
# This exists as a separate script, rather than as an `if` in the workflow, so the
# policy is in one place and can be unit-tested. Eight jobs consult it; a table that
# lived in YAML would be eight copies that drift.
#
# WHY EACH SKIP IS SAFE
#
#   skip-cxx     The change is documentation, prose or the Cloudflare worker. No lane
#                compiles any of it.
#
#   ui-app-only  The change touches only .cpp files under AestraUI/ or Source/ that no
#                headless target compiles. AESTRA_CI=ON force-disables AESTRA_ENABLE_UI
#                (CMakeLists.txt), and every lane except "Linux (UI/App compile)" sets
#                AESTRA_CI=ON — so no other lane has those files in its build at all.
#                This is the same fact that made the UI/App lane necessary (#396, #443):
#                the app could silently stop building for a week because nothing else
#                compiled it. Read in the other direction, it says exactly this.
#
# NOT skipped for ui-app-only, deliberately:
#
#   format       12 seconds. Gating it costs more in complexity than it saves.
#   ui-app       The one lane that can actually observe the change.

set -uo pipefail

VERDICT="${1:-}"
LANE="${2:-}"

# Lanes that compile with AESTRA_CI=ON, i.e. with the UI and the application
# force-disabled. None of them can observe a change confined to the UI/app trees.
is_headless_lane() {
    case "$1" in
        linux-gcc|windows-msvc|macos-clang|asan|tsan|lsan|tidy|plugin-host) return 0 ;;
        *) return 1 ;;
    esac
}

main() {
    case "${VERDICT}" in
        skip-cxx)
            # Nothing compiles prose. The UI/App lane included.
            case "${LANE}" in
                linux-gcc|windows-msvc|macos-clang|ui-app|asan|tsan|lsan|tidy|plugin-host)
                    echo "false"; return 0 ;;
            esac
            ;;
        ui-app-only)
            if is_headless_lane "${LANE}"; then
                echo "false"; return 0
            fi
            ;;
        broad)
            : # everything runs
            ;;
        *)
            : # unknown verdict: everything runs
            ;;
    esac

    echo "true"
}

main
