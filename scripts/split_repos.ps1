param(
	[string]$PublicRemote = "",
	[string]$PremiumRemote = "",
	[string]$BuildRemote = ""
)

$ErrorActionPreference = 'Stop'

function Require-Tool($name, $checkCmd) {
	try {
		& $checkCmd | Out-Null
	} catch {
		Write-Error "Required tool missing: $name"
		exit 1
	}
}

Require-Tool git { git --version }

# Optional: git-filter-repo is preferred
$hasFilterRepo = $false
try {
	git filter-repo --help | Out-Null
	$hasFilterRepo = $true
} catch {}

Write-Host "Starting split procedure..." -ForegroundColor Cyan

# Proposed structure (no moves yet):
#  /nomad-core      -> public
#  /nomad-premium   -> private
#  /nomad-build     -> private

Write-Host "Step 1: Create folders if not present (no moves performed)." -ForegroundColor Yellow
New-Item -ItemType Directory -Force -Path "nomad-core" | Out-Null
New-Item -ItemType Directory -Force -Path "nomad-premium" | Out-Null
New-Item -ItemType Directory -Force -Path "nomad-build" | Out-Null

Write-Host "Step 2: Manually move sensitive content into private folders before history rewrite:" -ForegroundColor Yellow
Write-Host " - Move premium code/models to .\\nomad-premium" -ForegroundColor Gray
Write-Host " - Move signing/build scripts to .\\nomad-build" -ForegroundColor Gray
Write-Host " - Keep core engine in .\\nomad-core" -ForegroundColor Gray

Write-Host "Step 3: Rewrite history to remove private content from public branch." -ForegroundColor Yellow
Write-Host " You can run one of the following (recommended: git filter-repo):" -ForegroundColor Gray

if ($hasFilterRepo) {
	Write-Host " Example (public branch cleanup):" -ForegroundColor Gray
	Write-Host " git checkout -B public-clean" -ForegroundColor DarkGray
	Write-Host " git filter-repo --path nomad-core --force" -ForegroundColor DarkGray
} else {
	Write-Host " Install git-filter-repo first: https://github.com/newren/git-filter-repo" -ForegroundColor Gray
	Write-Host " Or use BFG (requires Java): https://rtyley.github.io/bfg-repo-cleaner/" -ForegroundColor Gray
	Write-Host " BFG example (remove private folders from history):" -ForegroundColor DarkGray
	Write-Host "  git checkout -B public-clean" -ForegroundColor DarkGray
	Write-Host "  bfg --delete-folders nomad-premium,nomad-build --no-blob-protection" -ForegroundColor DarkGray
	Write-Host "  git reflog expire --expire=now --all && git gc --prune=now --aggressive" -ForegroundColor DarkGray
}

Write-Host "Step 4: Create separate private repositories and push." -ForegroundColor Yellow
if ($PremiumRemote) { Write-Host " - Push premium: (from a copy with full history) git push $PremiumRemote main" -ForegroundColor Gray }
if ($BuildRemote) { Write-Host " - Push build:   (from a copy with full history) git push $BuildRemote main" -ForegroundColor Gray }
if ($PublicRemote) { Write-Host " - Push public:  git push -u $PublicRemote public-clean:main" -ForegroundColor Gray }

Write-Host "Step 5: Optionally wire private repos as submodules in private super-repo:" -ForegroundColor Yellow
Write-Host " git submodule add <premium-remote> nomad-premium" -ForegroundColor DarkGray
Write-Host " git submodule add <build-remote>   nomad-build" -ForegroundColor DarkGray

Write-Host "Step 6: Verify public CI builds core only and no private paths remain." -ForegroundColor Yellow

Write-Host "Done. Review each step and run the commands suitable for your setup." -ForegroundColor Green
