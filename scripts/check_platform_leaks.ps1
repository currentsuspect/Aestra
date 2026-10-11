# Thin wrapper — the canonical implementation is scripts/check_platform_leaks.py
# (v0.8.0 FD-18: Python is the one implementation of each script check).
# This wrapper contains no check logic: it locates Python, runs the .py next
# to it (translating the legacy -Fix switch to --fix), and exits with its code.
param(
    [switch]$Fix = $false
)

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$pyScript = Join-Path $scriptDir "check_platform_leaks.py"

$pyArgs = @($pyScript)
if ($Fix) { $pyArgs += "--fix" }

if (Get-Command "python3" -ErrorAction SilentlyContinue) {
    & python3 @pyArgs
    exit $LASTEXITCODE
}
if (Get-Command "python" -ErrorAction SilentlyContinue) {
    & python @pyArgs
    exit $LASTEXITCODE
}
if (Get-Command "py" -ErrorAction SilentlyContinue) {
    & py -3 @pyArgs
    exit $LASTEXITCODE
}
Write-Error "No Python 3 interpreter found (tried python3, python, py -3). Cannot run check_platform_leaks.py."
exit 1
