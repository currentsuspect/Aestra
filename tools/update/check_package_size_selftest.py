#!/usr/bin/env python3
# © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
"""check_package_size.py passes a package that fits and refuses everything else, each with its own message."""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_package_size as cps  # noqa: E402

failures = []


def expect(ok, what):
    if not ok:
        failures.append(what)
        print("failed:", what)


with tempfile.TemporaryDirectory() as d:
    budget = os.path.join(d, "budget.txt")
    with open(budget, "w", encoding="utf-8") as f:
        f.write("# comment\nwindows-x64 1000  # trailing comment\nlinux-x64 500\n")

    def package(size):
        path = os.path.join(d, f"pkg-{size}.zip")
        with open(path, "wb") as f:
            f.write(b"\0" * size)
        return path

    ok, msg = cps.check("windows-x64", package(1000), budget)
    expect(ok and "100%" in msg, "a package exactly on its budget fits")
    ok, msg = cps.check("windows-x64", package(1001), budget)
    expect(not ok and "over the" in msg and "Nothing was published" in msg, "one byte over is refused")
    ok, msg = cps.check("linux-x64", package(1000), budget)
    expect(not ok, "each platform is held to its own row")
    ok, msg = cps.check("macos-arm64", package(10), budget)
    expect(not ok and "no row" in msg, "a platform without a row is refused, not waved through")
    ok, msg = cps.check("linux-x64", os.path.join(d, "missing.tar.gz"), budget)
    expect(not ok and "cannot read the package" in msg, "a missing package is refused")
    ok, msg = cps.check("linux-x64", package(10), os.path.join(d, "missing.txt"))
    expect(not ok and "budget file" in msg, "a missing budget file is refused")

    for text, what in (("linux-x64 abc\n", "a row that is not a number"), ("linux-x64 5 6\n", "a row with three fields"),
                       ("linux-x64 5\nlinux-x64 6\n", "a platform listed twice"), ("linux-x64 0\n", "a zero budget")):
        bad = os.path.join(d, "bad.txt")
        with open(bad, "w", encoding="utf-8") as f:
            f.write(text)
        ok, msg = cps.check("linux-x64", package(1), bad)
        expect(not ok and "budget file" in msg, f"{what} is refused")

    script = os.path.join(HERE, "check_package_size.py")
    run = subprocess.run([sys.executable, script, "windows-x64", package(1001), "--budget", budget], capture_output=True)
    expect(run.returncode == 1 and b"over the" in run.stderr, "the command exits 1 and explains on stderr")
    run = subprocess.run([sys.executable, script, "windows-x64", package(999), "--budget", budget], capture_output=True)
    expect(run.returncode == 0, "the command exits 0 for a package that fits")

    # The committed budget file parses and holds the two shipped platforms.
    shipped = cps.read_budgets(os.path.join(HERE, "size_budget.txt"))
    expect(shipped.get("windows-x64") == 8_000_000 and shipped.get("linux-x64") == 7_000_000,
           "the committed budgets are 8 MB for Windows and 7 MB for Linux (FD-77)")

print("check_package_size selftest:", "FAILED" if failures else "ok")
sys.exit(1 if failures else 0)
