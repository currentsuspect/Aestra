"""Install the git pre-commit hook (canonical installer).

Writes <hooks>/pre-commit as a POSIX sh script that runs
scripts/pre_commit_checks.py, so the hook works on Linux, macOS, and
Windows (via Git Bash/sh) without PowerShell. PowerShell users can call
scripts/install-hooks.ps1 instead, which is a thin wrapper around this file.

Exit code: 0 = ok, non-zero = refused or failed.
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

# Marker line written into every hook this installer creates. A pre-existing
# hook without this line is treated as unrelated and is never overwritten
# unless --force is passed.
MARKER = "# Installed by scripts/install_hooks.py (Aestra pre-commit hook). Do not edit by hand."

CHECKER_RELATIVE = Path("scripts") / "pre_commit_checks.py"


def run_git(args, cwd):
    """Run a git command, returning stripped stdout; raises RuntimeError."""
    result = subprocess.run(
        ["git"] + args, cwd=str(cwd), capture_output=True, text=True
    )
    if result.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)} failed: {result.stderr.strip()}")
    return result.stdout.strip()


def repo_root():
    """Repository top level, or None when not inside a repository."""
    try:
        top = run_git(["rev-parse", "--show-toplevel"], cwd=Path.cwd())
    except (RuntimeError, OSError, FileNotFoundError):
        return None
    if not top:
        return None
    return Path(top)


def hooks_dir(root):
    """Hooks directory for the current repository (worktree-aware).

    `git rev-parse --git-path hooks` resolves correctly when .git is a file
    (linked worktrees) rather than a directory. The result may be relative,
    in which case it is relative to the repository root.
    """
    out = run_git(["rev-parse", "--git-path", "hooks"], cwd=str(root))
    candidate = Path(out)
    if not candidate.is_absolute():
        candidate = root / candidate
    return candidate


def hook_content():
    """The POSIX sh hook script. It resolves the repository root via git at
    hook run time (git may invoke the hook from any directory), then runs
    the checker with python3 (falling back to python) and exits with its
    status so a failing check blocks the commit."""
    lines = [
        "#!/bin/sh",
        MARKER,
        'REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null)"',
        'if [ -z "$REPO_ROOT" ]; then',
        '    echo "pre-commit hook: cannot determine repository root (git rev-parse --show-toplevel failed)." >&2',
        "    exit 1",
        "fi",
        'CHECKER="$REPO_ROOT/scripts/pre_commit_checks.py"',
        'if [ ! -f "$CHECKER" ]; then',
        '    echo "pre-commit hook: checker not found: $CHECKER" >&2',
        "    exit 1",
        "fi",
        "if command -v python3 >/dev/null 2>&1; then",
        '    python3 "$CHECKER"',
        "    exit $?",
        "elif command -v python >/dev/null 2>&1; then",
        '    python "$CHECKER"',
        "    exit $?",
        "else",
        '    echo "pre-commit hook: no Python 3 interpreter found (tried python3, python)." >&2',
        "    exit 1",
        "fi",
        "",
    ]
    return "\n".join(lines)


def hook_has_marker(path):
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return False
    return MARKER in text.splitlines()


def describe_existing(path):
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        return f"unreadable ({exc})"
    first = text.splitlines()[0] if text.splitlines() else "<empty>"
    return f"{len(text)} bytes, first line: {first}"


def main():
    parser = argparse.ArgumentParser(
        description="Install (or uninstall) the Aestra pre-commit hook."
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Overwrite an existing pre-commit hook that was not installed by this script.",
    )
    parser.add_argument(
        "--uninstall",
        action="store_true",
        help="Remove the pre-commit hook, but only if it was installed by this script.",
    )
    args = parser.parse_args()

    root = repo_root()
    if root is None:
        print(
            "install_hooks: not inside a git repository "
            "(git rev-parse --show-toplevel failed).",
            file=sys.stderr,
        )
        return 1

    try:
        hooks = hooks_dir(root)
    except RuntimeError as exc:
        print(f"install_hooks: {exc}", file=sys.stderr)
        return 1
    target = hooks / "pre-commit"

    if args.uninstall:
        if not target.exists() and not target.is_symlink():
            print(f"No pre-commit hook at {target}; nothing to do.")
            return 0
        if not hook_has_marker(target):
            print(
                f"Refusing to remove {target}: it was not installed by this script "
                f"({describe_existing(target)}). Remove it by hand if intended.",
                file=sys.stderr,
            )
            return 1
        try:
            target.unlink()
        except OSError as exc:
            print(f"install_hooks: cannot remove {target}: {exc}", file=sys.stderr)
            return 1
        print(f"Removed pre-commit hook at {target}.")
        return 0

    if target.exists() or target.is_symlink():
        if hook_has_marker(target):
            action = "Re-installed"
        elif not args.force:
            print(
                f"Refusing to overwrite {target}: it was not installed by this script "
                f"({describe_existing(target)}). Pass --force to replace it.",
                file=sys.stderr,
            )
            return 1
        else:
            action = "Replaced (with --force)"
    else:
        action = "Installed"

    try:
        hooks.mkdir(parents=True, exist_ok=True)
        # open(newline=...) rather than Path.write_text(newline=...): the latter
        # only accepts newline on Python 3.10+, and this installer must run on
        # whatever interpreter a contributor has. LF is explicit because the hook
        # is executed by sh, which chokes on CRLF.
        with open(target, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(hook_content())
        os.chmod(target, 0o755)
    except OSError as exc:
        print(f"install_hooks: cannot write {target}: {exc}", file=sys.stderr)
        return 1

    print(f"{action} pre-commit hook at {target} (runs scripts/pre_commit_checks.py).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
