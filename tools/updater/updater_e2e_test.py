#!/usr/bin/env python3
# © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
"""Runs the real AestraUpdater program against fake installs (shell scripts standing in for Aestra).

    updater_e2e_test.py <path to AestraUpdater>

A good new version is installed and started; a new version that dies at once is rolled back and the old one is started
again; a broken package changes nothing; a user closing the old version too late changes nothing. Skipped on Windows
(the program is the same; the stand-ins are shell scripts).
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

if os.name == "nt":
    print("updater e2e: skipped on Windows")
    sys.exit(77)
UPDATER = sys.argv[1]
failures = []


def check(ok, what):
    if not ok:
        failures.append(what)
        print("failed:", what)


def program(marker, exit_code=0, sleep=0):
    return f"#!/bin/sh\necho started >> \"$(dirname \"$0\")/{marker}\"\nsleep {sleep}\nexit {exit_code}\n"


def world(root, old_script, new_script):
    app, staged = os.path.join(root, "Aestra"), os.path.join(root, "Aestra.new")
    for d, script in ((app, old_script), (staged, new_script)):
        os.makedirs(d)
        path = os.path.join(d, "Aestra")
        open(path, "w").write(script)
        os.chmod(path, 0o755)
    open(os.path.join(staged, "version.txt"), "w").write("new")
    open(os.path.join(app, "version.txt"), "w").write("old")
    return app, staged


def run(root, app, staged, *extra):
    result = os.path.join(root, "data", "last.json")
    cmd = [UPDATER, "--app-dir", app, "--staged", staged, "--backup", os.path.join(root, "Aestra.old"), "--exe", "Aestra",
           "--result", result, "--from", "0.8.1", "--to", "0.8.2", "--watch-seconds", "3", *extra]
    code = subprocess.run(cmd, timeout=60).returncode
    time.sleep(0.5)  # the relaunched stand-in writes its marker a moment after
    return code, (json.load(open(result)) if os.path.exists(result) else None)


with tempfile.TemporaryDirectory() as root:
    # 1. A good update.
    d = os.path.join(root, "good"); os.makedirs(d)
    app, staged = world(d, program("old-ran"), program("new-ran", sleep=5))
    code, res = run(d, app, staged)
    check(code == 0 and res and res["ok"], f"good update: exit 0 and ok (got {code} {res})")
    check(open(os.path.join(app, "version.txt")).read() == "new", "good update: the new version is in place")
    check(open(os.path.join(d, "Aestra.old", "version.txt")).read() == "old", "good update: the old one waits in the backup")
    check(os.path.exists(os.path.join(app, "new-ran")), "good update: the new version was started")
    check(not os.path.exists(os.path.join(app, "old-ran")), "good update: the old version was not")

    # 2. The new version dies at once: back to the old one, and it is started again.
    d = os.path.join(root, "crash"); os.makedirs(d)
    app, staged = world(d, program("old-ran"), program("new-ran", exit_code=1))
    code, res = run(d, app, staged)
    check(code == 4 and res and not res["ok"] and res["rolledBack"], f"crashing update: exit 4, rolled back (got {code} {res})")
    check(open(os.path.join(app, "version.txt")).read() == "old", "crashing update: the old version is back")
    check(os.path.exists(os.path.join(app, "old-ran")), "crashing update: the old version was started again")
    check(not os.path.exists(os.path.join(d, "Aestra.old")), "crashing update: no backup litter")

    # 3. A package with no program: nothing changes, the old version is started again.
    d = os.path.join(root, "broken"); os.makedirs(d)
    app, staged = world(d, program("old-ran"), program("new-ran"))
    os.remove(os.path.join(staged, "Aestra"))
    code, res = run(d, app, staged)
    check(code == 2 and res and not res["ok"], f"broken package: exit 2 (got {code} {res})")
    check(open(os.path.join(app, "version.txt")).read() == "old" and os.path.exists(os.path.join(app, "old-ran")), "broken package: old version untouched and started")

    # 4. The old version never closes: nothing is touched.
    d = os.path.join(root, "busy"); os.makedirs(d)
    app, staged = world(d, program("old-ran"), program("new-ran"))
    sleeper = subprocess.Popen(["sleep", "30"])
    try:
        code, res = run(d, app, staged, "--pid", str(sleeper.pid), "--wait-seconds", "1")
    finally:
        sleeper.kill()
        sleeper.wait()
    check(code == 3 and res and not res["ok"], f"old version still running: exit 3 (got {code} {res})")
    check(open(os.path.join(app, "version.txt")).read() == "old" and os.path.isdir(staged), "old version still running: nothing was changed")

    # 5. The old version closes while the updater waits: then it proceeds.
    d = os.path.join(root, "wait"); os.makedirs(d)
    app, staged = world(d, program("old-ran"), program("new-ran", sleep=5))
    closing = subprocess.Popen(["sleep", "1"])
    code, res = run(d, app, staged, "--pid", str(closing.pid))
    closing.wait()
    check(code == 0 and res and res["ok"], f"waits for the old version to close, then updates (got {code} {res})")

    # 6. Missing required arguments.
    check(subprocess.run([UPDATER], capture_output=True).returncode == 64, "no arguments: refuses")

print("updater e2e:", "FAILED" if failures else "passed")
sys.exit(1 if failures else 0)
