#!/usr/bin/env python3
# © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#
# Verify that every registered CTest case carries a valid policy identity.
#
# Usage:
#   verify-test-contracts.py --allowlist <file> --maximal <label>
#                            --census <label>=<ctest-json> [--census ...]
#
# Exit: 0 = every configuration satisfies the policy. 1 = it does not.
#
# THE CONTRACT
#
#   Normal registered case      exactly one known contract:*, and no role:*
#   Allowlisted benchmark       zero contract:*, and exactly role:benchmark
#   Anything else               fail
#
# The benchmark exemption needs BOTH keys. `role:benchmark` alone does not
# exempt a case, and an allowlist entry alone does not either. That is the whole
# point of the two-key design: adding a label to a failing test buys nothing
# without a separate, reviewable change to the authority file.
#
# WHAT THIS DELIBERATELY DOES NOT DO
#
# It does not hardcode today's census — not 243 cases, not 242 contracts, not
# the current distribution. Those numbers prove one migration; baking them in
# would block correctly classified tests from ever being added. The allowlist
# decides the approved benchmark set; the structural rules decide whether a
# census is valid. A guard that freezes a number stops being a policy and
# becomes a snapshot.
#
# It reads enumerated CTest JSON, never CMake source. Parsing lost to
# enumerating three times during the classification migration: tests live in ten
# registration files, registration is conditional, and a label can be attached
# far from the add_test() that created the case.

import argparse
import json
import sys

KNOWN_CONTRACTS = frozenset(
    {
        "contract:security",
        "contract:durability",
        "contract:audio",
        "contract:realtime",
        "contract:plugins",
        "contract:application",
        "contract:core",
    }
)

KNOWN_ROLES = frozenset({"role:benchmark"})

CONTRACT_PREFIX = "contract:"
ROLE_PREFIX = "role:"


class PolicyError(Exception):
    """A diagnosable violation. Message is the operator-facing diagnostic."""


# --- allowlist -------------------------------------------------------------
#
# The format is narrow on purpose. Every tolerance a parser grants is a
# tolerance the authority file has to keep honouring, and an authority that
# accepts comments today accepts `# temporarily exempt everything` tomorrow.


def parse_allowlist(path):
    try:
        with open(path, "rb") as handle:
            raw = handle.read()
    except OSError as exc:
        raise PolicyError(f"allowlist unreadable: {path}: {exc}") from exc

    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise PolicyError(f"allowlist is not valid UTF-8: {path}: {exc}") from exc

    if "\r" in text:
        raise PolicyError(f"allowlist contains a carriage return: {path}")

    if text and not text.endswith("\n"):
        raise PolicyError(f"allowlist does not end with a newline: {path}")

    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines = lines[:-1]  # only the terminal LF is ignored

    entries = []
    seen = set()
    for number, line in enumerate(lines, start=1):
        if line == "":
            raise PolicyError(f"allowlist line {number} is blank: {path}")
        if line != line.strip():
            raise PolicyError(
                f"allowlist line {number} has leading or trailing whitespace: {line!r}"
            )
        if line in seen:
            raise PolicyError(f"allowlist line {number} duplicates {line!r}")
        seen.add(line)
        entries.append(line)

    return entries


# --- census ----------------------------------------------------------------


def parse_census(label, path):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            document = json.load(handle)
    except OSError as exc:
        raise PolicyError(f"[{label}] census unreadable: {path}: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise PolicyError(f"[{label}] census is not valid JSON: {path}: {exc}") from exc

    if not isinstance(document, dict) or "tests" not in document:
        raise PolicyError(f"[{label}] census has no 'tests' array: {path}")

    tests = document["tests"]
    if not isinstance(tests, list):
        raise PolicyError(f"[{label}] census 'tests' is not an array: {path}")

    # Duplicates are diagnosed BEFORE the list becomes a set. Collapsing them
    # silently would let a broken registration hide: two add_test() calls with
    # the same name and different labels would produce one entry, with whichever
    # came last winning, and the policy would pass while the tree was ambiguous.
    census = {}
    for entry in tests:
        if not isinstance(entry, dict) or "name" not in entry:
            raise PolicyError(f"[{label}] census contains a test with no name: {path}")
        name = entry["name"]
        labels = []
        for prop in entry.get("properties", []) or []:
            if isinstance(prop, dict) and prop.get("name") == "LABELS":
                value = prop.get("value", [])
                labels = list(value) if isinstance(value, list) else [value]
        if name in census:
            raise PolicyError(
                f"[{label}] census registers {name!r} more than once: {path}"
            )
        census[name] = labels

    if not census:
        raise PolicyError(f"[{label}] census registers no tests: {path}")

    return census


# --- per-configuration policy ----------------------------------------------


def check_configuration(label, census, allowlist, failures):
    allowed = set(allowlist)

    for name in sorted(census):
        labels = census[name]
        contracts = sorted(l for l in labels if l.startswith(CONTRACT_PREFIX))
        roles = sorted(l for l in labels if l.startswith(ROLE_PREFIX))

        unknown_contracts = [l for l in contracts if l not in KNOWN_CONTRACTS]
        if unknown_contracts:
            failures.append(
                f"[{label}] {name}: unknown contract label(s) {unknown_contracts}"
            )
            continue

        unknown_roles = [l for l in roles if l not in KNOWN_ROLES]
        if unknown_roles:
            failures.append(f"[{label}] {name}: unknown role label(s) {unknown_roles}")
            continue

        is_allowlisted = name in allowed
        has_benchmark_role = "role:benchmark" in roles

        if is_allowlisted or has_benchmark_role:
            # THE PER-CONFIGURATION RULE, which is the operative one:
            #
            #     benchmark_role_cases(C) == allowlist ∩ registered_cases(C)
            #
            # Enforced here as a biconditional on each registered case: a role
            # carrier must be allowlisted, and a registered allowlist entry must
            # carry the role. This is what makes an EXPERIMENTAL_TESTS=OFF
            # configuration correct with *zero* carriers — the benchmark is not
            # in registered_cases(C), so the intersection is empty and the
            # equality holds without a special case.
            #
            # The global rule in check_across() is a different statement:
            #
            #     union(benchmark_role_cases(C)) == allowlist
            #     allowlist ⊆ union(registered_cases(C))
            #
            # which is what rejects a stale entry naming nothing at all.
            #
            # Both keys are required. Either one alone is a policy violation,
            # reported specifically so the fix is obvious.
            if not is_allowlisted:
                failures.append(
                    f"[{label}] {name}: carries role:benchmark but is not in the "
                    f"benchmark allowlist"
                )
                continue
            if not has_benchmark_role:
                failures.append(
                    f"[{label}] {name}: is in the benchmark allowlist but does not "
                    f"carry role:benchmark"
                )
                continue
            if contracts:
                failures.append(
                    f"[{label}] {name}: is an allowlisted benchmark and must carry no "
                    f"contract, but carries {contracts}"
                )
                continue
            if roles != ["role:benchmark"]:
                failures.append(
                    f"[{label}] {name}: allowlisted benchmark must carry exactly "
                    f"role:benchmark, but carries {roles}"
                )
            continue

        if roles:
            failures.append(f"[{label}] {name}: normal case must carry no role, has {roles}")
            continue
        if len(contracts) == 0:
            failures.append(f"[{label}] {name}: has no contract label")
            continue
        if len(contracts) > 1:
            failures.append(f"[{label}] {name}: has {len(contracts)} contracts {contracts}, expected exactly one")


# --- cross-configuration policy --------------------------------------------


def check_across(censuses, maximal_label, allowlist, failures):
    union = set()
    for census in censuses.values():
        union |= set(census)

    if maximal_label not in censuses:
        failures.append(f"maximal configuration {maximal_label!r} was not measured")
        return

    maximal = set(censuses[maximal_label])
    if maximal != union:
        missing = sorted(union - maximal)
        failures.append(
            f"maximal configuration {maximal_label!r} is not the union: "
            f"{len(missing)} case(s) registered elsewhere but not there: {missing[:5]}"
        )

    # A repeated name must have the same POLICY IDENTITY wherever it is
    # registered — its contract:* and role:* labels only.
    #
    # Descriptive labels are deliberately excluded. They are outside this policy
    # and may legitimately vary by lane: a case could reasonably carry `headless`
    # in one configuration and not another. Requiring total label equality would
    # make the guard enforce a rule nobody agreed to, and would block a
    # descriptive change that has no bearing on which promise the test protects.
    for name in sorted(union):
        seen = {}
        for label, census in censuses.items():
            if name in census:
                identity = tuple(
                    sorted(
                        l
                        for l in census[name]
                        if l.startswith(CONTRACT_PREFIX) or l.startswith(ROLE_PREFIX)
                    )
                )
                seen.setdefault(identity, []).append(label)
        if len(seen) > 1:
            variants = "; ".join(
                f"{list(v)} -> {list(k)}" for k, v in sorted(seen.items())
            )
            failures.append(
                f"{name}: policy identity differs between configurations: {variants}"
            )

    # THE GLOBAL RULE:
    #
    #     union(benchmark_role_cases(C)) == allowlist
    #     allowlist ⊆ union(registered_cases(C))
    #
    # The per-configuration biconditional in check_configuration() already
    # guarantees the two keys agree wherever a case is registered. What is left
    # for the global rule is the case that is registered NOWHERE: an allowlist
    # entry naming a test that no configuration produces. Nothing per
    # configuration can see that, because the name never appears.
    role_carriers = {
        name
        for census in censuses.values()
        for name, labels in census.items()
        if "role:benchmark" in labels
    }
    stale = sorted(name for name in allowlist if name not in union)
    if stale:
        failures.append(
            f"allowlist entries absent from the measured union: {stale}"
        )

    if role_carriers != set(allowlist) - set(stale):
        only_role = sorted(role_carriers - set(allowlist))
        only_list = sorted((set(allowlist) - set(stale)) - role_carriers)
        failures.append(
            "benchmark keys disagree across configurations: "
            f"role:benchmark only = {only_role}, allowlist only = {only_list}"
        )


# --- entry point -----------------------------------------------------------


def run(allowlist_path, maximal_label, census_args):
    failures = []

    allowlist = parse_allowlist(allowlist_path)

    # A repeated --census label is diagnosed rather than silently overwritten.
    # Same defect class as a duplicate test name inside one census: the second
    # value would replace the first, one configuration would vanish from the
    # comparison, and every remaining check would still pass. A coverage
    # authority must not contain a silent-collapse path even where no caller
    # currently exercises it.
    censuses = {}
    for spec in census_args:
        if "=" not in spec:
            raise PolicyError(f"--census expects <label>=<path>, got {spec!r}")
        label, path = spec.split("=", 1)
        if label in censuses:
            raise PolicyError(f"--census label {label!r} was given more than once")
        censuses[label] = parse_census(label, path)

    if not censuses:
        raise PolicyError("no configurations were measured")

    for label in sorted(censuses):
        check_configuration(label, censuses[label], allowlist, failures)

    check_across(censuses, maximal_label, allowlist, failures)

    return failures, censuses, allowlist


def main(argv=None):
    parser = argparse.ArgumentParser(description="Verify test contract coverage.")
    parser.add_argument("--allowlist", required=True)
    parser.add_argument("--maximal", required=True)
    parser.add_argument("--census", action="append", default=[], metavar="LABEL=PATH")
    args = parser.parse_args(argv)

    try:
        failures, censuses, allowlist = run(args.allowlist, args.maximal, args.census)
    except PolicyError as exc:
        print(f"::error::{exc}")
        return 1

    for label in sorted(censuses):
        census = censuses[label]
        contracted = sum(
            1 for labels in census.values() if any(l.startswith(CONTRACT_PREFIX) for l in labels)
        )
        benchmarks = sum(1 for labels in census.values() if "role:benchmark" in labels)
        print(f"  {label}: {len(census)} registered, {contracted} contracted, {benchmarks} benchmark")

    print(f"  allowlist: {len(allowlist)} entry(ies)")

    if failures:
        for failure in failures:
            print(f"::error::{failure}")
        print(f"\nTest contract coverage FAILED with {len(failures)} violation(s).")
        return 1

    print("\nTest contract coverage OK.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
