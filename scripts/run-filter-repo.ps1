<#
Safe wrapper to run git-filter-repo on a mirror clone. This script makes a mirror copy first and runs the filter there to avoid accidental damage.

USAGE (example):
  .\scripts\run-filter-repo.ps1 -PathsToRemove "NomadMuse/models,NomadAssets" -OutputDir "../nomad-cleaned"

This script DOES NOT push changes to any remote. It creates a cleaned repo in the `-OutputDir` location for verification.
#>
param(
    [Parameter(Mandatory=$true)]
    [string] $PathsToRemove,
    [string] $OutputDir = "../nomad-cleaned"
)

function Fail($msg){ Write-Error $msg; exit 1 }

if (-not (Get-Command git-filter-repo -ErrorAction SilentlyContinue)) {
    Write-Host "git-filter-repo not found in PATH. Install it: https://github.com/newren/git-filter-repo" -ForegroundColor Yellow
}

$mirrorDir = Join-Path -Path (Get-Location) -ChildPath "../nomad-mirror"
if (Test-Path $mirrorDir) { Fail "Mirror dir already exists: $mirrorDir. Remove or rename it before running." }

Write-Host "Creating bare mirror clone..." -ForegroundColor Cyan
git clone --mirror . $mirrorDir
if ($LASTEXITCODE -ne 0) { Fail "git clone --mirror failed." }

Push-Location $mirrorDir

$paths = $PathsToRemove -split ',' | ForEach-Object { $_.Trim() }
Write-Host "Paths to remove from history:" $paths

# Build filter-repo arguments
$args = @()
foreach ($p in $paths) {
    $args += "--path"
    $args += $p
}
$args += "--invert-paths"
$args += "--force"

Write-Host "Running git-filter-repo in mirror repository (this will rewrite history in $mirrorDir)..." -ForegroundColor Yellow
git-filter-repo @args
if ($LASTEXITCODE -ne 0) { Fail "git-filter-repo failed." }

Pop-Location

Write-Host "Creating cleaned clone for verification: $OutputDir" -ForegroundColor Cyan
if (Test-Path $OutputDir) { Fail "Output dir exists: $OutputDir. Remove or choose another path." }
git clone $mirrorDir $OutputDir
if ($LASTEXITCODE -ne 0) { Fail "git clone of cleaned mirror failed." }

Write-Host "Cleaned repo available at: $OutputDir" -ForegroundColor Green
Write-Host "IMPORTANT: Inspect the cleaned repo, run gitleaks and build checks before pushing anywhere." -ForegroundColor Yellow
