#!/usr/bin/env bash
# © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#
# Compile-time audio-thread constraint check. Implements the second half of
# B-005 (v0.7.1 trust sprint, objective TS-1, T-2).
#
# Usage:  check-rt-effects.sh [build-dir]
# Exit:   0 = every annotated translation unit is provably non-blocking.
#         1 = a violation, or the check could not be run.
#
# WHAT THIS ACTUALLY CHECKS
#
# Clang's -Wfunction-effects walks the call graph out of every function carrying
# [[clang::nonblocking]] (spelled AESTRA_RT_NONBLOCKING in this tree) and
# diagnoses allocation, deallocation, locks, throws, atomic waits, thread-local
# access, indirect calls it cannot resolve, and calls to any function it cannot
# prove non-blocking. That is the forbidden list from THREADING_MODEL.md, minus
# file I/O and sleeps, which arrive as "calls a function it cannot prove".
#
# It is a *type system* check, not a heuristic and not a grep: the obligation
# propagates through the call graph, so annotating one entry point pulls in
# everything it reaches.
#
# COVERAGE IS THE ANNOTATION SET, AND IT IS DERIVED, NOT DECLARED
#
# There is deliberately no allowlist file. The set of checked translation units
# is computed as "every .cpp under the audio tree that mentions
# AESTRA_RT_NONBLOCKING", so coverage cannot drift away from the annotations the
# way a hand-maintained list does. Add an annotation and its TU is gated on the
# next run; nobody has to remember a second edit.
#
# The honest boundary: a TU that merely *calls* annotated functions without
# annotating anything itself is not compiled here. It does not need to be — the
# diagnostic for an annotated function's body is emitted where that body is, and
# an unannotated caller is making no claim to violate.
#
# FAIL-CLOSED, EVERY PATH
#
# Like check-decision-citation.sh and unlike lane-runs.sh, everything that is not
# a positively verified pass exits 1: no compile_commands.json, no clang, an
# annotated TU with no compile command, a compiler that does not implement the
# attribute. A constraint check that reports success when it did not run is worse
# than no check, because it certifies a claim nobody tested.
set -uo pipefail

BUILD_DIR="${1:-build-clang}"
# Self-locating by default, so the tree under check is the tree this script was
# committed into and cannot be redirected by an unlucky working directory.
# AESTRA_RT_EFFECT_ROOT is a test seam and nothing else — check-rt-effects.test.sh
# sets it to point at a throwaway fixture. CI never sets it.
REPO_ROOT="${AESTRA_RT_EFFECT_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
DB="${BUILD_DIR}/compile_commands.json"

if [[ ! -d "$BUILD_DIR" ]]; then
    echo "FAIL: build directory '${BUILD_DIR}' does not exist."
    echo "      Configure one with:"
    echo "        cmake -S . -B ${BUILD_DIR} -DCMAKE_CXX_COMPILER=clang++ \\"
    echo "              -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DAESTRA_RT_EFFECT_CHECK=ON"
    exit 1
fi

if [[ ! -f "$DB" ]]; then
    echo "FAIL: ${DB} not found — reconfigure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON."
    exit 1
fi

CXX="${CXX:-clang++}"
if ! command -v "$CXX" >/dev/null 2>&1; then
    echo "FAIL: '${CXX}' not on PATH. This check requires Clang; GCC has no"
    echo "      function-effects analysis and the annotations are inert there."
    exit 1
fi

# Prove the compiler actually implements the attribute before trusting a pass.
# A Clang too old to know [[clang::nonblocking]] parses the annotation as an
# unknown attribute and cheerfully compiles every violation in the tree.
probe="$(mktemp -t rt-effects-probe-XXXXXX.cpp)"
probe_out="$(mktemp -t rt-effects-probe-XXXXXX.log)"
trap 'rm -f "$probe" "$probe_out"' EXIT
cat >"$probe" <<'PROBE'
void violation() [[clang::nonblocking]] { delete new int(1); }
PROBE
if ! "$CXX" -std=c++20 -Wfunction-effects -fsyntax-only "$probe" 2>"$probe_out"; then
    echo "FAIL: probe translation unit did not compile at all:"
    sed 's/^/      /' "$probe_out"
    exit 1
fi
if ! grep -q "function-effects" "$probe_out"; then
    echo "FAIL: ${CXX} did not diagnose a deliberate allocation inside a"
    echo "      [[clang::nonblocking]] function. The attribute is not supported"
    echo "      here, so a pass would be meaningless. Clang 20+ is required."
    "$CXX" --version | sed 's/^/      /'
    exit 1
fi

# The annotated translation units, derived from the annotations themselves.
#
# The set is "every .cpp that COMPILES annotated code", not "every .cpp that
# mentions the macro". The distinction is not academic: every built-in effect
# is header-only, so BuiltInPlugins.cpp includes a dozen headers carrying
# AESTRA_RT_NONBLOCKING while itself never naming it. Under the older
# --include='*.cpp' rule those annotations were invisible to this gate, which is
# precisely the drift this script exists to prevent — it reported three
# translation units while the audio tree had annotations in eleven headers.
#
# So the derivation walks the include graph: a .cpp is in the set when anything
# it transitively includes mentions the macro. Quoted includes are resolved
# against the including file's own directory and then against that
# translation unit's -I paths, because that is what the compiler does.
# Angle-bracket includes are skipped: they name system or vendored headers,
# which are not this tree's annotations to gate.
#
# Python does the walking rather than shell. This file already depends on it to
# extract compile arguments, and a transitive closure in bash is both slower and
# far easier to get quietly wrong.
mapfile -t ANNOTATED < <(
    cd "$REPO_ROOT" &&
    python3 - "$DB" "$REPO_ROOT" AESTRA_RT_NONBLOCKING <<'PY'
import json, os, re, shlex, sys

db_path, root, macro = sys.argv[1], sys.argv[2], sys.argv[3]

# -I roots per translation unit, so a quoted include resolves the way the
# compiler will resolve it rather than by guessing at the layout.
inc_dirs = {}
try:
    for entry in json.load(open(db_path)):
        argv = shlex.split(entry.get("command") or "") if entry.get("command") \
            else list(entry["arguments"])
        dirs = []
        for i, a in enumerate(argv):
            if a == "-I" and i + 1 < len(argv):
                dirs.append(os.path.abspath(argv[i + 1]))
            elif a.startswith("-I") and len(a) > 2:
                dirs.append(os.path.abspath(a[2:]))
        inc_dirs[os.path.abspath(entry["file"])] = dirs
except Exception:
    pass  # No database is handled and reported by the caller, not here.

INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)


def mentions_macro(path):
    try:
        with open(path, "r", errors="replace") as fh:
            return macro in fh.read()
    except OSError:
        return False


def resolve(name, from_file, tu_dirs):
    """Resolve a quoted include the way the compiler will.

    tu_dirs are the ORIGINAL translation unit's -I paths, and they are
    threaded through every level of the walk on purpose. inc_dirs is keyed by
    translation unit, so a nested header has no entry of its own -- a header
    included only via the TU's -I paths (AestraSat.h reaching DSP/Oversampler.h
    through -IAestraAudio/include) would otherwise resolve against nothing and
    be silently skipped, which is the failure this whole derivation exists to
    prevent. The including file's own directory still wins, exactly as the
    compiler's quoted-include lookup does.
    """
    for d in [os.path.dirname(from_file), *tu_dirs]:
        cand = os.path.join(d, name)
        if os.path.isfile(cand):
            return os.path.abspath(cand)
    return None


# Seeds: any file in the tree carrying an annotation, .h or .cpp.
seeds = set()
for dirpath, dirnames, filenames in os.walk(root):
    dirnames[:] = [d for d in dirnames if d not in (".git", "build", "External", "external")]
    for fn in filenames:
        if fn.endswith((".h", ".hpp", ".cpp")):
            p = os.path.join(dirpath, fn)
            if mentions_macro(p):
                seeds.add(os.path.abspath(p))

# Candidates are the union of two sets, and both are needed.
#
#   1. Every translation unit in the compile database. These are what this
#      configuration actually builds, and each is followed through the include
#      graph -- that is how a header-only plugin is reached at all.
#
#   2. Every .cpp ON DISK that mentions the macro directly, whether or not this
#      build compiles it. This is the old rule, kept deliberately: it is what
#      makes the gate fail closed on an annotated unit that has no compile
#      command instead of quietly dropping it. Dropping it would turn "this
#      configuration does not build it" into a silent pass.
#
# A file in neither set is a TU this configuration does not build
# (Source/App/AestraApp.cpp in a headless run), so there is nothing here that
# could check it -- and it carries no annotation of its own.
try:
    db_entries = json.load(open(db_path))
except Exception:
    db_entries = []

# An empty database is NOT an early exit. The on-disk set still applies, and
# the caller then reports each annotated unit as "no compile command" -- which
# is the fail-closed answer, and strictly better than aborting here with a
# vaguer message. Bailing on an empty list would have turned the silent gap
# this rule exists to close into a differently-worded pass.
built = sorted({os.path.abspath(e["file"]) for e in db_entries if e.get("file")})

on_disk = []
for dirpath, dirnames, filenames in os.walk(root):
    dirnames[:] = [d for d in dirnames if d not in (".git", "build", "External", "external")]
    for fn in filenames:
        if fn.endswith(".cpp"):
            p = os.path.abspath(os.path.join(dirpath, fn))
            if p in seeds:
                on_disk.append(p)

found = []
for cpp in sorted(set(built) | set(on_disk)):
    if cpp in seeds:
        found.append(cpp)
        continue
    # Transitive closure with a visited set, so a diamond include does not loop
    # and a cyclic #include (guarded, so legal) terminates. tu_dirs is fixed for
    # the whole walk: it is this TU's include path, and every nested header
    # resolves against it, not against an entry of its own.
    tu_dirs = inc_dirs.get(cpp, [])
    seen, stack, hit = set(), [cpp], False
    while stack and not hit:
        cur = stack.pop()
        if cur in seen:
            continue
        seen.add(cur)
        if cur in seeds and cur != cpp:
            hit = True
            break
        try:
            text = open(cur, "r", errors="replace").read()
        except OSError:
            continue
        for name in INCLUDE.findall(text):
            nxt = resolve(name, cur, tu_dirs)
            if nxt and nxt not in seen:
                stack.append(nxt)
    if hit:
        found.append(cpp)

for path in sorted(set(found)):
    print(os.path.relpath(path, root))
PY
)

if [[ ${#ANNOTATED[@]} -eq 0 ]]; then
    echo "FAIL: no translation unit carries or reaches an AESTRA_RT_NONBLOCKING"
    echo "      annotation. Either the annotations were removed or the macro was"
    echo "      renamed. An empty check must not report success."
    exit 1
fi

echo "RT effect check — ${#ANNOTATED[@]} annotated translation unit(s)"
echo "compiler: $("$CXX" --version | head -1)"
echo

failed=0
checked=0
for rel in "${ANNOTATED[@]}"; do
    abs="${REPO_ROOT}/${rel}"
    # Pull this TU's real compile line out of the database, so the check sees
    # the same macros and include paths the build does. A hand-rolled command
    # line drifts, and a drifted one silently checks a different program.
    # NUL-separated into a bash array, never a shell string.
    #
    # This used to print the args shell-quoted and expand them unquoted. That is
    # wrong in a way that stays invisible until a flag needs quoting: word
    # splitting does NOT re-parse quote characters, so clang received a literal
    #
    #     '-DAESTRA_VERSION_STRING="0.7.1"'
    #
    # quotes and all, and reported it as a missing file. Every flag in the tree
    # happened to be quote-free until a version string with embedded quotes was
    # added, at which point the gate failed on its own plumbing rather than on
    # anything it was checking.
    #
    # A temp file rather than a pipeline because python's exit status has to be
    # readable: process substitution would give us mapfile's status instead, and
    # an empty array is ambiguous between "no compile command" and "no flags".
    argfile="$(mktemp -t rt-effects-args-XXXXXX)"
    python3 - "$DB" "$abs" "$argfile" <<'PY'
import json, shlex, sys
db_path, target, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
for entry in json.load(open(db_path)):
    if entry["file"] == target:
        argv = shlex.split(entry.get("command") or "")[1:] if entry.get("command") \
               else list(entry["arguments"])[1:]
        keep = []
        skip_next = False
        for a in argv:
            if skip_next:
                skip_next = False
                continue
            # Drop output and compile-only flags; -fsyntax-only replaces them.
            if a in ("-o", "-c"):
                skip_next = a == "-o"
                continue
            if a == target or a.endswith(".o"):
                continue
            keep.append(a)
        with open(out_path, "w") as fh:
            fh.write("\0".join(keep))
        break
else:
    sys.exit(3)
PY
    if [[ $? -ne 0 ]]; then
        rm -f "$argfile"
        echo "FAIL: ${rel} carries AESTRA_RT_NONBLOCKING but has no compile"
        echo "      command in ${DB}. It is annotated and unchecked, which is"
        echo "      exactly the gap this script exists to prevent."
        failed=1
        continue
    fi
    args=()
    mapfile -t -d '' args < "$argfile"
    rm -f "$argfile"

    out="$(mktemp -t rt-effects-XXXXXX.log)"
    if "$CXX" "${args[@]}" -Wfunction-effects -Werror=function-effects \
              -fsyntax-only "$abs" >"$out" 2>&1; then
        echo "  ok    ${rel}"
    else
        echo "  FAIL  ${rel}"
        sed 's/^/        /' "$out"
        failed=1
    fi
    rm -f "$out"
    checked=$((checked + 1))
done

echo
if [[ $failed -ne 0 ]]; then
    echo "RT effect check FAILED."
    echo
    echo "Each diagnostic above names a function that an audio-thread path"
    echo "reaches and the compiler cannot prove non-blocking. Three ways out,"
    echo "in order of preference:"
    echo
    echo "  1. Make it non-blocking. Usually the right answer on an RT path."
    echo "  2. Annotate it AESTRA_RT_NONBLOCKING at its DECLARATION, so callers"
    echo "     in other translation units see the contract too. 'no definition"
    echo "     in this translation unit' always means this."
    echo "  3. Move it off the RT path, and guard it with reportRealtimeMisuse."
    echo
    echo "Waiving the diagnostic is not on that list. If a waiver is genuinely"
    echo "correct, it belongs beside the argument that makes it correct — see"
    echo "the two in RealtimeThreadGuard.h for the standard that has to meet."
    exit 1
fi

echo "RT effect check passed — ${checked} translation unit(s) provably non-blocking."
exit 0
