#!/usr/bin/env python3
# © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
"""Write the signed update feed for one build (see AestraLicense/include/UpdateFeed.h for the format).

    make_feed.py --package Aestra-0.8.2-windows-x64.zip --channel early --platform windows-x64 \
                 --version 0.8.2 --url-base https://update.aestra.studio --notes notes.txt --out feed

Writes  <out>/<channel>/<platform>/latest.json  and copies the package next to it. The Ed25519 private key is read from
the environment variable AESTRA_UPDATE_SIGNING_KEY (64 hex chars, the 32-byte seed) and never from a file in the repo.
`--verify <pubkey-hex>` re-reads the written feed and checks the signature, so a CI job fails before publishing a feed
the app would refuse.
"""
import argparse
import hashlib
import json
import os
import shutil
import sys
from datetime import datetime, timezone

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey

CHANNELS = ("stable", "early")
PLATFORMS = ("windows-x64", "linux-x64", "macos-arm64")


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def build_payload(package, channel, platform, version, url_base, notes):
    name = os.path.basename(package)
    return json.dumps(
        {
            "schema": 1,
            "channel": channel,
            "platform": platform,
            "version": version,
            "published": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "notes": notes,
            "url": f"{url_base.rstrip('/')}/{channel}/{platform}/{name}",
            "sha256": sha256_of(package),
            "size": os.path.getsize(package),
        },
        separators=(",", ":"),
        ensure_ascii=False,
    )


def sign(payload, seed_hex):
    key = Ed25519PrivateKey.from_private_bytes(bytes.fromhex(seed_hex))
    return key.sign(payload.encode("utf-8")).hex()


def verify(document_path, public_hex):
    with open(document_path, encoding="utf-8") as f:
        doc = json.load(f)
    Ed25519PublicKey.from_public_bytes(bytes.fromhex(public_hex)).verify(
        bytes.fromhex(doc["signature"]), doc["payload"].encode("utf-8")
    )


def public_hex(seed_hex):
    key = Ed25519PrivateKey.from_private_bytes(bytes.fromhex(seed_hex))
    return key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw).hex()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--package", required=True)
    ap.add_argument("--channel", required=True, choices=CHANNELS)
    ap.add_argument("--platform", required=True, choices=PLATFORMS)
    ap.add_argument("--version", required=True)
    ap.add_argument("--url-base", required=True)
    ap.add_argument("--notes", default="", help="path to a text file with the release notes")
    ap.add_argument("--out", required=True)
    ap.add_argument("--verify", metavar="PUBKEY_HEX", help="check the written feed against this public key")
    args = ap.parse_args()

    seed = os.environ.get("AESTRA_UPDATE_SIGNING_KEY", "").strip()
    if len(seed) != 64:
        sys.exit("AESTRA_UPDATE_SIGNING_KEY must hold the 64-hex-char Ed25519 seed")
    if not args.url_base.startswith("https://"):
        sys.exit("--url-base must be https://")
    notes = open(args.notes, encoding="utf-8").read().strip() if args.notes else ""

    target = os.path.join(args.out, args.channel, args.platform)
    os.makedirs(target, exist_ok=True)
    shutil.copy2(args.package, target)
    payload = build_payload(args.package, args.channel, args.platform, args.version, args.url_base, notes)
    feed = os.path.join(target, "latest.json")
    with open(feed, "w", encoding="utf-8") as f:
        json.dump({"payload": payload, "signature": sign(payload, seed)}, f, ensure_ascii=False)
    if args.verify:
        try:
            verify(feed, args.verify)
        except InvalidSignature:
            sys.exit("the written feed does not verify against the given public key")
    print(feed)


if __name__ == "__main__":
    main()
