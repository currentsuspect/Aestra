#!/usr/bin/env python3


import argparse
import os
import subprocess
import sys
import time
from pathlib import Path


def read_memory():
    values = {}
    for line in Path("/proc/meminfo").read_text().splitlines():
        key, value = line.split(":", 1)
        parts = value.strip().split()
        values[key] = int(parts[0]) * 1024

    total = values["MemTotal"]
    available = values["MemAvailable"]
    used = total - available
    return total, available, used


def write_summary(elapsed, initial_used, peak_used, memory_delta, minimum_available):
    summary_path = os.environ.get("GITHUB_STEP_SUMMARY")

    if not summary_path:
        return

    with open(summary_path, "a") as summary:
        summary.write("## Build resource measurements\n")
        summary.write(f"- Duration: {elapsed:.2f} seconds\n")
        summary.write(
            f"- Initial system memory used: {initial_used / (1024**3):.2f} GiB\n"
        )
        summary.write(f"- Peak system memory used: {peak_used / (1024**3):.2f} GiB\n")
        summary.write(
            f"- Peak system memory increase: {memory_delta / (1024**3):.2f} GiB\n"
        )
        summary.write(
            f"- Minimum system available memory: {minimum_available / (1024**3):.2f} GiB\n"
        )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command

    if command and command[0] == "--":
        command = command[1:]

    if not command:
        parser.error("no command supplied; use -- followed by a command")

    _, initial_available, initial_used = read_memory()

    peak_used = initial_used
    minimum_available = initial_available

    start_time = time.monotonic()

    try:
        process = subprocess.Popen(command)
    except FileNotFoundError:
        parser.error(f"command not found: {command[0]}")

    while process.poll() is None:
        _, available, used = read_memory()

        peak_used = max(peak_used, used)
        minimum_available = min(minimum_available, available)

        time.sleep(0.25)

    memory_delta = peak_used - initial_used
    elapsed = time.monotonic() - start_time

    print(f"Duration: {elapsed:.2f} seconds")
    print(f"Initial system memory used: {initial_used / (1024**3):.2f} GiB")
    print(f"Peak system memory used: {peak_used / (1024**3):.2f} GiB")
    print(f"Peak system memory increase: {memory_delta / (1024**3):.2f} GiB")
    print(f"Minimum system available memory: {minimum_available / (1024**3):.2f} GiB")

    write_summary(
        elapsed,
        initial_used,
        peak_used,
        memory_delta,
        minimum_available,
    )

    return process.returncode


if __name__ == "__main__":
    sys.exit(main())
