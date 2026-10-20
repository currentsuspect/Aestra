#!/usr/bin/env bash
# © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
# Publish one build to update.aestra.studio: sign the feed, upload the package, then the feed (the Worker refuses a feed
# whose package is not there yet, so the order matters and a half-finished upload never goes live).
#
#   publish.sh <channel: stable|early> <platform> <version> <package file> [notes file]
#
# Environment: AESTRA_UPDATE_SIGNING_KEY (Ed25519 seed, hex), AESTRA_UPDATE_UPLOAD_KEY (the Worker's UPLOAD_KEY),
#              AESTRA_UPDATE_PUBLIC_KEY (hex; the written feed is re-checked against it before anything is uploaded).
set -euo pipefail

channel=${1:?channel}; platform=${2:?platform}; version=${3:?version}; package=${4:?package}; notes=${5:-}
base=${AESTRA_UPDATE_BASE:-https://update.aestra.studio}
here=$(cd "$(dirname "$0")" && pwd)
: "${AESTRA_UPDATE_UPLOAD_KEY:?}" "${AESTRA_UPDATE_SIGNING_KEY:?}" "${AESTRA_UPDATE_PUBLIC_KEY:?}"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
notes_args=(); [ -n "$notes" ] && notes_args=(--notes "$notes")
python3 "$here/make_feed.py" --package "$package" --channel "$channel" --platform "$platform" --version "$version" \
  --url-base "$base" "${notes_args[@]}" --out "$work" --verify "$AESTRA_UPDATE_PUBLIC_KEY" >/dev/null

dir="$work/$channel/$platform"
name=$(basename "$package")
for f in "$name" latest.json; do
  code=$(curl -sS -o "$work/reply" -w '%{http_code}' --retry 3 --max-time 600 -X PUT \
    -H "Authorization: Bearer $AESTRA_UPDATE_UPLOAD_KEY" --data-binary "@$dir/$f" "$base/$channel/$platform/$f")
  if [ "$code" != 200 ]; then echo "upload of $f failed: HTTP $code $(cat "$work/reply")" >&2; exit 1; fi
done
echo "published $version to $channel/$platform"
