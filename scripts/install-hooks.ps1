# Thin wrapper — the canonical implementation is scripts/install_hooks.py
# (v0.8.0 FD-18: Python is the one implementation of each script check).
# This wrapper contains no install logic: it locates Python, runs the .py next
# to it with the same arguments, and exits with its exit code.
# PowerShell users run this; everyone else runs `python3 scripts/install_hooks.py`.

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$pyScript = Join-Path $scriptDir "install_hooks.py"
if (-not (Test-Path $pyScript)) {
    $repoRoot = git rev-parse --show-toplevel 2>$null
    if ($repoRoot) { $pyScript = Join-Path $repoRoot.Trim() "scripts/install_hooks.py" }
}
if (-not (Test-Path $pyScript)) {
    Write-Error "install_hooks.py not found (looked beside this script and in <repo>/scripts)."
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
Write-Error "No Python 3 interpreter found (tried python3, python, py -3). Cannot run install_hooks.py."
exit 1
