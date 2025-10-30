<#
Helper to add private repos as submodules for local development.

Usage:
  .\scripts\add-submodules.ps1 -PremiumRepoUrl git@github.com:Org/nomad-premium.git

This script only adds submodules locally. It does not push or change remotes.
#>
param(
    [Parameter(Mandatory=$true)]
    [string] $PremiumRepoUrl,
    [string] $PremiumPath = "NomadMuse"
)

Write-Host "Adding premium submodule: $PremiumRepoUrl -> $PremiumPath" -ForegroundColor Cyan
git submodule add $PremiumRepoUrl $PremiumPath
if ($LASTEXITCODE -ne 0) { Write-Error "git submodule add failed"; exit 1 }

Write-Host "Initializing and updating submodules..." -ForegroundColor Cyan
git submodule update --init --recursive

Write-Host "Submodule added. Remember to commit the .gitmodules change if you intend to keep it." -ForegroundColor Green
