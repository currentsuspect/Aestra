"""Pre-commit secret/sensitive-file scanner (canonical implementation).

Unifies scripts/pre-commit-checks.ps1 (staged file content scan) and
scripts/pre-commit.ps1 (staged filename blocklist plus staged-diff secret
scan) into one Python script. Both .ps1 files are thin wrappers around this
file. PowerShell ``-match`` is case-insensitive, so every pattern below is
compiled with ``re.IGNORECASE``.

Exit code: 0 = ok, non-zero = block commit.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

# Filename blocklist. Union of the pre-commit.ps1 blocked-path patterns and
# the .h5 extension blocked only by pre-commit-checks.ps1. Git reports staged
# paths with '/' separators on every platform, so (^|/) works everywhere.
FILENAME_PATTERNS = [
    # From pre-commit.ps1 (blockedPaths):
    r'\.key$',
    r'\.pem$',
    r'\.pfx$',
    r'\.jks$',
    r'\.keystore$',
    r'\.p12$',
    r'\.crt$',
    r'\.cer$',
    r'\.asc$',
    r'^\.env(\..*)?$',
    r'(^|/)secrets(/|$)',
    r'(^|/)credentials(/|$)',
    r'(^|/)signing(/|$)',
    r'(^|/)codesign(/|$)',
    r'(^|/)notarize(/|$)',
    r'(^|/)provisioning(/|$)',
    r'(^|/)assets_premium(/|$)',
    r'(^|/)assets_private(/|$)',
    r'(^|/)weights(/|$)',
    r'(^|/)models(/|$)',
    r'\.(onnx|pt|pth|safetensors|tflite|pb)$',
    # From pre-commit-checks.ps1 filename block (the only addition is .h5;
    # the rest are already covered above but are matched here per-file so the
    # message names the extension, as the .ps1 did):
    r'\.h5$',
]

# Content patterns checked against each staged file's working-tree text.
# Union of the pre-commit-checks.ps1 pattern list and the pre-commit.ps1
# secret hints (the hints were diff-only in the .ps1; checking them against
# file text too is a strict superset). The .ps1 listed both 'api_key' and
# 'API_KEY' and both AKIA spellings; under IGNORECASE each pair collapses to
# one entry with identical behaviour.
CONTENT_PATTERNS = [
    # From pre-commit-checks.ps1:
    r'-----BEGIN .*PRIVATE KEY-----',
    r'BEGIN PRIVATE KEY',
    r'\.pfx$',
    r'\.p12$',
    r'\.pem$',
    r'\.key$',
    r'AestraCert',
    r'AKIA[0-9A-Z]{16}',
    r'ssh-rsa AAAA',
    r'api_key',
    r'password\s*=\s*',
    r'\.onnx$',
    r'\.pt$',
    r'\.pth$',
    r'\.h5$',
    r'\.ckpt$',
    # From pre-commit.ps1 secret hints:
    r'AWS_(ACCESS|SECRET)_KEY',
    r'SECRET[_-]?KEY',
    r'PRIVATE[_-]?KEY',
    r'-----BEGIN (RSA|OPENSSH|EC) PRIVATE KEY-----',
    r'ghp_[0-9A-Za-z]{36,}',
    r'github_pat_[0-9A-Za-z_]{20,}',
    r'xox[baprs]-[0-9A-Za-z-]{10,}',
    r'password\s*=\s*["\']?.{6,}["\']?',
]

# Subset of CONTENT_PATTERNS checked against the full staged diff, exactly as
# pre-commit.ps1 scanned `git diff --cached` for secret hints. This catches
# secrets in staged hunks even when the working-tree file is absent
# (e.g. staged deletions) or differs from the staged blob.
DIFF_PATTERNS = [
    r'AWS_(ACCESS|SECRET)_KEY',
    r'AKIA[0-9A-Z]{16}',
    r'SECRET[_-]?KEY',
    r'PRIVATE[_-]?KEY',
    r'-----BEGIN (RSA|OPENSSH|EC) PRIVATE KEY-----',
    r'ghp_[0-9A-Za-z]{36,}',
    r'github_pat_[0-9A-Za-z_]{20,}',
    r'xox[baprs]-[0-9A-Za-z-]{10,}',
    r'password\s*=\s*["\']?.{6,}["\']?',
]

# Explicit blocked extensions, mirroring the pre-commit-checks.ps1 filename
# check so the message names the extension as it did there.
BLOCKED_EXTENSIONS = ('.pfx', '.p12', '.pem', '.key', '.onnx', '.pt', '.pth', '.h5', '.ckpt')

FILENAME_RES = [(p, re.compile(p, re.IGNORECASE)) for p in FILENAME_PATTERNS]
CONTENT_RES = [(p, re.compile(p, re.IGNORECASE)) for p in CONTENT_PATTERNS]
DIFF_RES = [(p, re.compile(p, re.IGNORECASE)) for p in DIFF_PATTERNS]


def git(args, cwd):
    """Run a git command, returning stdout text; raises on failure."""
    result = subprocess.run(['git'] + args, cwd=cwd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)} failed: {result.stderr.strip()}")
    return result.stdout


def repo_root():
    """Repository top level for the current directory (hook runs at root)."""
    try:
        top = git(['rev-parse', '--show-toplevel'], cwd=str(Path.cwd()))
        return Path(top.strip())
    except RuntimeError:
        return Path.cwd()


def main():
    parser = argparse.ArgumentParser(
        description='Pre-commit checks: scan staged files for secrets and blocked paths.'
    )
    parser.parse_args()

    root = repo_root()

    try:
        staged = git(['diff', '--cached', '--name-only'], cwd=str(root)).splitlines()
    except RuntimeError as exc:
        # Fail closed: a secret scanner that cannot see the index must block.
        print(f"Pre-commit check failed: {exc}")
        return 1

    staged = [line for line in staged if line.strip()]
    if not staged:
        print('No staged files.')
        return 0

    failures = []
    seen = set()

    def add(message):
        if message not in seen:
            seen.add(message)
            failures.append(message)

    for staged_file in staged:
        for pattern, rx in FILENAME_RES:
            if rx.search(staged_file):
                add(f"{staged_file} => filename pattern: {pattern}")
        lowered = staged_file.lower()
        for ext in BLOCKED_EXTENSIONS:
            if lowered.endswith(ext):
                add(f"{staged_file} => blocked extension {ext}")

        candidate = root / staged_file
        try:
            is_file = candidate.is_file()
        except OSError:
            is_file = False
        if not is_file:
            continue  # Staged deletion or missing file: filename checks above still apply.
        try:
            text = candidate.read_text(encoding='utf-8', errors='replace')
        except OSError:
            continue
        if text is None:
            continue
        for pattern, rx in CONTENT_RES:
            if rx.search(text):
                add(f"{staged_file} => pattern: {pattern}")

    try:
        diff = git(['diff', '--cached'], cwd=str(root))
    except RuntimeError as exc:
        print(f"Pre-commit check failed: {exc}")
        return 1
    for pattern, rx in DIFF_RES:
        if rx.search(diff):
            add(f"staged diff => pattern: {pattern}")

    if failures:
        print('Pre-commit check failed. Sensitive patterns found:')
        for failure in failures:
            print(failure)
        print('If this is a false positive, review and stage amended files. '
              'Otherwise remove secrets and try again.')
        return 1

    print('Pre-commit checks passed.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
