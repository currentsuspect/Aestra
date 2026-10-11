<#
Deprecated: use scripts/install-hooks.ps1 (or `python3 scripts/install_hooks.py`) instead.
This file is a thin shim that forwards to the canonical installer,
scripts/install_hooks.py, and exits with its exit code.
#>

Write-Host "install-pre-commit.ps1 is deprecated; scripts/install-hooks.ps1 (or `python3 scripts/install_hooks.py`) is the supported installer."

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
