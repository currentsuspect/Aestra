#!/usr/bin/env python3
# © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
"""Refuse a package that is over its download size budget (FD-77).

    check_package_size.py <platform> <package file> [--budget size_budget.txt]

Exit 0 when the package fits. Exit 1 when it is too large, when the platform has no budget row (a new platform
gets a budget before its first publish), or when the budget file or the package cannot be read. Never a skip.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def read_budgets(path):
    """{platform: bytes}. Raises ValueError on a malformed or duplicated row."""
    budgets = {}
    with open(path, encoding="utf-8") as f:
        for number, line in enumerate(f, 1):
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) != 2 or not parts[1].isdigit() or int(parts[1]) <= 0:
                raise ValueError(f"{path}:{number}: expected '<platform> <bytes>', got '{line}'")
            if parts[0] in budgets:
                raise ValueError(f"{path}:{number}: '{parts[0]}' has two rows")
            budgets[parts[0]] = int(parts[1])
    return budgets


def megabytes(size):
    return f"{size / 1e6:.2f} MB"


def check(platform, package, budget_path):
    """(ok, message)."""
    try:
        budgets = read_budgets(budget_path)
    except (OSError, ValueError) as e:
        return False, f"size budget: cannot read the budget file: {e}"
    if platform not in budgets:
        return False, f"size budget: '{platform}' has no row in {os.path.basename(budget_path)}; add one before publishing"
    try:
        size = os.path.getsize(package)
    except OSError as e:
        return False, f"size budget: cannot read the package: {e}"
    budget = budgets[platform]
    name = os.path.basename(package)
    if size > budget:
        return False, (f"size budget: {name} is {megabytes(size)}, over the {megabytes(budget)} budget for {platform} "
                       f"by {megabytes(size - budget)}. Nothing was published. Make the package smaller, or raise the "
                       f"row in tools/update/size_budget.txt with a decision that allows it (FD-77).")
    return True, f"size budget: {name} is {megabytes(size)} of {megabytes(budget)} for {platform} ({size * 100 // budget}%)"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("platform")
    ap.add_argument("package")
    ap.add_argument("--budget", default=os.path.join(HERE, "size_budget.txt"))
    args = ap.parse_args()
    ok, message = check(args.platform, args.package, args.budget)
    print(message, file=sys.stdout if ok else sys.stderr)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
