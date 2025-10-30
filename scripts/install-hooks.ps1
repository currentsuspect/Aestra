param()

$ErrorActionPreference = 'Stop'

$repoRoot = (git rev-parse --show-toplevel).Trim()
$hooksDir = Join-Path $repoRoot ".git/hooks"
$targetHook = Join-Path $hooksDir "pre-commit"
$template = Join-Path $repoRoot "scripts/pre-commit.ps1"

if (-not (Test-Path $hooksDir)) {
	New-Item -ItemType Directory -Force -Path $hooksDir | Out-Null
}

if (-not (Test-Path $template)) {
	Write-Error "Hook template not found: $template"
	exit 1
}

$shim = @" 
#!/usr/bin/env pwsh
& "$PSScriptRoot/pre-commit.ps1"
"@

Set-Content -Path $targetHook -Value $shim -NoNewline
Copy-Item -Force -Path $template -Destination (Join-Path $hooksDir "pre-commit.ps1")

Write-Host "Installed pre-commit hook to $hooksDir" -ForegroundColor Green
