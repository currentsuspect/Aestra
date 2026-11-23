#!/usr/bin/env bash
# Real-renderer design previews: see tools/ui/theme_preview.py (--help) and docs/developer/ui-theme-preview.md.
exec python3 "$(dirname "$0")/theme_preview.py" "$@"
