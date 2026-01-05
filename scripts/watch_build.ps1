[CmdletBinding()]
param(
  [ValidateSet('headless','full')]
  [string]$Preset = 'full',

  [ValidateSet('Debug','Release','RelWithDebInfo','MinSizeRel')]
  [string]$Config = 'Release',

  [string]$Target = 'NomadHeadless',

  [int]$DebounceMs = 750,

  [switch]$Once,

  [string]$ScenarioFile,
  [string]$ScenarioName,
  [string]$ReportPath
)

$ErrorActionPreference = 'Stop'

function Invoke-NomadBuild {
  $buildPreset = if ($Preset -eq 'headless') {
    if ($Config -eq 'Debug') { 'headless-debug' } else { 'headless-release' }
  } else {
    'full-release'
  }

  Write-Host "[watch_build] Building via preset '$buildPreset'..." -ForegroundColor Cyan

  # Ensure configure exists (cmake preset build will do this too, but configure errors are clearer first).
  cmake --preset $Preset

  if ($Preset -eq 'headless') {
    cmake --build --preset $buildPreset
  } else {
    cmake --build --preset $buildPreset
  }

  Write-Host "[watch_build] Build OK" -ForegroundColor Green
}

function Invoke-HeadlessRun {
  if ($Preset -ne 'headless') { return }
  if ([string]::IsNullOrWhiteSpace($ScenarioFile) -and [string]::IsNullOrWhiteSpace($ScenarioName) -and [string]::IsNullOrWhiteSpace($ReportPath)) {
    return
  }

  $exeCandidates = @(
    (Join-Path $PSScriptRoot ("..\\build\\headless\\bin\\$Config\\NomadHeadless.exe")),
    Join-Path $PSScriptRoot '..\build\headless\bin\Release\NomadHeadless.exe',
    Join-Path $PSScriptRoot '..\build\headless\bin\Debug\NomadHeadless.exe'
  )

  $exe = $exeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
  if (-not $exe) {
    Write-Host "[watch_build] NomadHeadless.exe not found for run" -ForegroundColor Yellow
    return
  }

  $args = @()
  if ($ScenarioFile) { $args += @('--scenario-file', $ScenarioFile) }
  if ($ScenarioName) { $args += @('--scenario', $ScenarioName) }
  if ($ReportPath) { $args += @('--report', $ReportPath) }

  Write-Host "[watch_build] Running NomadHeadless..." -ForegroundColor Cyan
  & $exe @args
  $code = $LASTEXITCODE
  if ($code -ne 0) {
    Write-Host "[watch_build] Headless run FAILED (exit=$code)" -ForegroundColor Red
  } else {
    Write-Host "[watch_build] Headless run OK" -ForegroundColor Green
  }
}

# Initial build
Invoke-NomadBuild
Invoke-HeadlessRun

if ($Once) { exit 0 }

$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$watchDirs = @(
  Join-Path $root 'Source',
  Join-Path $root 'NomadAudio',
  Join-Path $root 'NomadCore',
  Join-Path $root 'NomadPlugins',
  Join-Path $root 'Tests',
  Join-Path $root 'cmake'
) | Where-Object { Test-Path $_ }

Write-Host "[watch_build] Watching for changes (preset=$Preset config=$Config target=$Target)" -ForegroundColor Cyan
Write-Host "[watch_build] Ctrl+C to stop" -ForegroundColor DarkGray

$script:pending = $false
$script:lastChange = Get-Date

$handlers = @()
foreach ($dir in $watchDirs) {
  $fsw = New-Object System.IO.FileSystemWatcher
  $fsw.Path = $dir
  $fsw.IncludeSubdirectories = $true
  $fsw.EnableRaisingEvents = $true
  $fsw.NotifyFilter = [IO.NotifyFilters]'FileName, LastWrite, Size, DirectoryName'

  $action = {
    param($sender, $eventArgs)

    $p = $eventArgs.FullPath
    if ($p -match '\\build\\' -or $p -match '\\cmake-build-' -or $p -match '\\.git\\') { return }

    if ($p -notmatch '\.(c|cc|cpp|cxx|h|hh|hpp|hxx|inl|ixx|cmake|json|md)$' -and ($p -notmatch 'CMakeLists\.txt$')) {
      return
    }

    $script:pending = $true
    $script:lastChange = Get-Date
  }

  $handlers += Register-ObjectEvent -InputObject $fsw -EventName Changed -Action $action
  $handlers += Register-ObjectEvent -InputObject $fsw -EventName Created -Action $action
  $handlers += Register-ObjectEvent -InputObject $fsw -EventName Deleted -Action $action
  $handlers += Register-ObjectEvent -InputObject $fsw -EventName Renamed -Action $action
}

try {
  while ($true) {
    Start-Sleep -Milliseconds 100

    if ($script:pending) {
      $elapsed = (Get-Date) - $script:lastChange
      if ($elapsed.TotalMilliseconds -ge $DebounceMs) {
        $script:pending = $false
        try {
          Invoke-NomadBuild
          Invoke-HeadlessRun
        } catch {
          Write-Host "[watch_build] Build FAILED" -ForegroundColor Red
          Write-Host $_.Exception.Message -ForegroundColor Red
        }
      }
    }
  }
} finally {
  foreach ($h in $handlers) { Unregister-Event -SourceIdentifier $h.Name -ErrorAction SilentlyContinue }
}
