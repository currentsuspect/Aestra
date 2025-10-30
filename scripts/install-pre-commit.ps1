<#
Install the git pre-commit hook that calls the PowerShell scanner.
Run from repo root: .\scripts\install-pre-commit.ps1
#>
param()

$hookPath = Join-Path -Path (Resolve-Path -Path .git).Path -ChildPath "hooks\pre-commit"
Write-Host "Installing pre-commit hook to: $hookPath"

$hookContent = @"
#!/usr/bin/env pwsh
# Auto-generated pre-commit hook: calls the repository PowerShell scanner
pwsh -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot/../scripts/pre-commit-checks.ps1"
if (
    $LASTEXITCODE -ne 0
) {
    echo "pre-commit check failed"
    exit 1
}
exit 0
"@

# Ensure hooks directory exists
if (-not (Test-Path (Split-Path -Path $hookPath -Parent))) {
    New-Item -ItemType Directory -Path (Split-Path -Path $hookPath -Parent) -Force | Out-Null
}

# Write hook file
[System.IO.File]::WriteAllText($hookPath, $hookContent)

# Make sure the hook is executable (Git for Windows will honor the file)
git update-index --add --chmod=+x .git/hooks/pre-commit 2>$null

Write-Host "Pre-commit hook installed. Run 'git add' and commit as normal. The scanner will run automatically." -ForegroundColor Green
