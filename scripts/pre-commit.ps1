# Thin wrapper — the canonical implementation is scripts/pre_commit_checks.py
# (v0.8.0 FD-18: Python is the one implementation of each script check).
# This wrapper contains no check logic: it locates Python, runs the .py next
# to it with the same arguments, and exits with its exit code.

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$pyScript = Join-Path $scriptDir "pre_commit_checks.py"
# scripts/install-hooks.ps1 copies this wrapper into .git/hooks, where the .py is not
# alongside it; fall back to the repository copy.
if (-not (Test-Path $pyScript)) {
    $repoRoot = git rev-parse --show-toplevel 2>$null
    if ($repoRoot) { $pyScript = Join-Path $repoRoot.Trim() "scripts/pre_commit_checks.py" }
}
if (-not (Test-Path $pyScript)) {
    Write-Error "pre_commit_checks.py not found (looked beside this script and in <repo>/scripts)."
    exit 1
}

if (Get-Command "python3" -ErrorAction SilentlyContinue) {
    & python3 $pyScript @args
    exit $LASTEXITCODE
}
if (Get-Command "python" -ErrorAction SilentlyContinue) {
    & python $pyScript @args
    exit $LASTEXITCODE
}
if (Get-Command "py" -ErrorAction SilentlyContinue) {
    & py -3 $pyScript @args
    exit $LASTEXITCODE
}
Write-Error "No Python 3 interpreter found (tried python3, python, py -3). Cannot run pre_commit_checks.py."
exit 1
