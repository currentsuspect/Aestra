param(
	[string]$PremiumRemote,
	[string]$BuildRemote,
	[string]$PremiumPath = "nomad-premium",
	[string]$BuildPath = "nomad-build",
	[switch]$Update
)

$ErrorActionPreference = 'Stop'

if ($Update) {
	if (Test-Path .gitmodules) {
		git submodule sync --recursive
		git submodule update --init --recursive
		Write-Host "Submodules synced and updated." -ForegroundColor Green
		exit 0
	} else {
		Write-Error ".gitmodules not found; cannot update."
		exit 1
	}
}

if (-not $PremiumRemote -or -not $BuildRemote) {
	Write-Error "Provide -PremiumRemote and -BuildRemote URLs."
	exit 1
}

# Add premium submodule
if (-not (Test-Path $PremiumPath)) {
	git submodule add $PremiumRemote $PremiumPath
} else {
	Write-Host "$PremiumPath already exists; skipping add." -ForegroundColor Yellow
}

# Add build submodule
if (-not (Test-Path $BuildPath)) {
	git submodule add $BuildRemote $BuildPath
} else {
	Write-Host "$BuildPath already exists; skipping add." -ForegroundColor Yellow
}

# Initialize and update
git submodule update --init --recursive
Write-Host "Submodules added/initialized." -ForegroundColor Green
