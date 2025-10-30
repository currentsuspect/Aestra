# NOMAD Build Script
Write-Host "`n==================================" -ForegroundColor Cyan
Write-Host "  NOMAD DAW - Build System" -ForegroundColor Cyan
Write-Host "==================================" -ForegroundColor Cyan

# Check for command line arguments
$doClean = $false
foreach ($arg in $args) {
    if ($arg -eq "-clean" -or $arg -eq "--clean") {
        $doClean = $true
    }
    elseif ($arg -eq "-help" -or $arg -eq "--help") {
        Write-Host "NOMAD DAW Build Script" -ForegroundColor Cyan
        Write-Host ""
        Write-Host "Usage: .\build.ps1 [options]"
        Write-Host ""
        Write-Host "Options:"
        Write-Host "  -clean    Clean build (rebuild from scratch)"
        Write-Host "  -help     Show this help message"
        Write-Host ""
        Write-Host "Examples:"
        Write-Host "  .\build.ps1           # Normal build"
        Write-Host "  .\build.ps1 -clean    # Clean rebuild"
        exit 0
    }
}

# Clean build if requested
if ($doClean) {
    Write-Host "`nCleaning previous build..." -ForegroundColor Yellow
    if (Test-Path "build") {
        Remove-Item -Path "build" -Recurse -Force
    }
    Write-Host "Clean complete!" -ForegroundColor Green
}

# Step 1: Clone FreeType if needed
$freetypeDir = "build\_deps\freetype-src"
if (-not (Test-Path $freetypeDir)) {
    Write-Host "`n[1/4] Cloning FreeType..." -ForegroundColor Yellow
    New-Item -ItemType Directory -Path "build\_deps" -Force | Out-Null
    git clone --depth 1 --branch VER-2-13-3 https://github.com/freetype/freetype.git $freetypeDir
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Failed to clone FreeType!" -ForegroundColor Red
        exit 1
    }
    Write-Host "FreeType cloned!" -ForegroundColor Green
} else {
    Write-Host "`n[1/4] FreeType already exists" -ForegroundColor Green
}

# Step 2: Configure CMake
Write-Host "`n[2/4] Configuring CMake..." -ForegroundColor Yellow
cmake -B build -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) {
    Write-Host "Configuration failed!" -ForegroundColor Red
    exit 1
}
Write-Host "Configuration complete!" -ForegroundColor Green

# Step 3: Build all NOMAD components
Write-Host "`n[3/4] Building NOMAD libraries..." -ForegroundColor Yellow
cmake --build build --config Release --parallel
if ($LASTEXITCODE -ne 0) {
    Write-Host "Library build failed!" -ForegroundColor Red
    exit 1
}
Write-Host "Libraries built successfully!" -ForegroundColor Green

# Step 4: Build the complete NOMAD DAW application
Write-Host "`n[4/4] Building NOMAD DAW executable..." -ForegroundColor Yellow
cmake --build build --config Release --target NOMAD_DAW
if ($LASTEXITCODE -ne 0) {
    Write-Host "Application build failed!" -ForegroundColor Red
    exit 1
}
Write-Host "NOMAD DAW built successfully!" -ForegroundColor Green

# Check if executable exists
$exePath = "build\bin\Release\NOMAD_DAW.exe"
if (Test-Path $exePath) {
    $exeSize = (Get-Item $exePath).Length
    Write-Host "Executable size: $([math]::Round($exeSize / 1MB, 2)) MB" -ForegroundColor Cyan
} else {
    Write-Host "Warning: Executable not found at expected location" -ForegroundColor Yellow
}

Write-Host "`n==================================" -ForegroundColor Cyan
Write-Host "  NOMAD DAW Build Complete!" -ForegroundColor Green
Write-Host "  Executable: build\bin\Release\NOMAD_DAW.exe" -ForegroundColor Cyan
Write-Host "==================================" -ForegroundColor Cyan
Write-Host ""
