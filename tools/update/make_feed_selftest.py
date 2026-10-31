#!/usr/bin/env python3
# © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
"""make_feed.py writes a feed that verifies, carries the right hash and size, and refuses to run without a key."""
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
try:
    import make_feed  # noqa: E402
except ImportError:  # the signing library is not installed here: skip, do not fail
    print("make_feed selftest: skipped (python 'cryptography' not installed)")
    sys.exit(77)

SEED = "00" * 31 + "09"  # throwaway
failures = []


def check(ok, what):
    if not ok:
        failures.append(what)
        print("failed:", what)


with tempfile.TemporaryDirectory() as d:
    pkg = os.path.join(d, "Aestra-0.8.2-windows-x64.zip")
    with open(pkg, "wb") as f:
        f.write(b"PK" + os.urandom(1000))
    notes = os.path.join(d, "notes.txt")
    open(notes, "w", encoding="utf-8").write("Plugin windows. Naïve ✓ \"quoted\"\n")
    out = os.path.join(d, "feed")
    base = [sys.executable, os.path.join(HERE, "make_feed.py"), "--package", pkg, "--channel", "early", "--platform",
            "windows-x64", "--version", "0.8.2", "--url-base", "https://update.example", "--notes", notes, "--out", out]
    env = dict(os.environ, AESTRA_UPDATE_SIGNING_KEY=SEED)
    pub = make_feed.public_hex(SEED)

    r = subprocess.run(base + ["--verify", pub], env=env, capture_output=True, text=True)
    check(r.returncode == 0, "feed is written and verifies: " + r.stderr)
    feed = os.path.join(out, "early", "windows-x64", "latest.json")
    doc = json.load(open(feed, encoding="utf-8"))
    p = json.loads(doc["payload"])
    check(p["sha256"] == make_feed.sha256_of(pkg) and p["size"] == os.path.getsize(pkg), "hash and size match the package")
    check(p["url"] == "https://update.example/early/windows-x64/Aestra-0.8.2-windows-x64.zip", "url is base/channel/platform/file")
    check(p["notes"] == 'Plugin windows. Naïve ✓ "quoted"', "notes survive unicode and quotes")
    check(os.path.exists(os.path.join(out, "early", "windows-x64", os.path.basename(pkg))), "package copied next to the feed")

    try:
        make_feed.verify(feed, make_feed.public_hex("00" * 31 + "0a"))
        check(False, "another key must not verify")
    except Exception:
        pass

    r = subprocess.run(base, env=dict(os.environ, AESTRA_UPDATE_SIGNING_KEY=""), capture_output=True, text=True)
    check(r.returncode != 0, "no key, no feed")
    r = subprocess.run([a if a != "https://update.example" else "http://update.example" for a in base], env=env,
                       capture_output=True, text=True)
    check(r.returncode != 0, "an http url base is refused")

print("make_feed selftest:", "FAILED" if failures else "passed")
sys.exit(1 if failures else 0)
