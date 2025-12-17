# Platform Abstraction Leak Detection Script
# This script checks for Windows-specific code leaks outside NomadPlat/src/Win32/

param(
    [switch]$Fix = $false
)

$ErrorActionPreference = "Stop"
$violations = @()
$allowedPaths = @(
    "NomadPlat\src\Win32",
    "NomadPlat\src\Win32\WinHeaders.h",
    "NomadAudio\src\Win32",
    "NomadUI\External",  # External libraries (glad, rtaudio) may have Windows code
    "NomadAudio\External"
)

# Forbidden Windows includes
$forbiddenIncludes = @(
    "windows\.h",
    "winuser\.h",
    "dwmapi\.h",
    "mmdeviceapi\.h",
    "audioclient\.h",
    "shellapi\.h",
    "shlobj\.h",
    "wrl\.h",
    "combaseapi\.h",
    "objbase\.h",
    "ole2\.h"
)

# Forbidden Windows types in headers (outside Win32 implementation)
$forbiddenTypes = @(
    "HWND",
    "HINSTANCE",
    "HRESULT",
    "DWORD",
    "HANDLE",
    "LRESULT",
    "WPARAM",
    "LPARAM",
    "GUID",
    "RECT",
    "POINT",
    "MSG"
)

# Forbidden Windows macros
$forbiddenMacros = @(
    "WINAPI",
    "CALLBACK",
    "__stdcall",
    "__declspec"
)

function Test-IsAllowedPath {
    param([string]$filePath)
    
    foreach ($allowed in $allowedPaths) {
        if ($filePath -like "*\$allowed\*" -or $filePath -like "$allowed\*") {
            return $true
        }
    }
    return $false
}

function Test-IsHeaderFile {
    param([string]$filePath)
    return $filePath -match '\.(h|hpp)$'
}

Write-Host "Scanning for Windows platform leaks..." -ForegroundColor Cyan

# Check for forbidden includes
Write-Host "`nChecking for forbidden Windows includes..." -ForegroundColor Yellow
Get-ChildItem -Recurse -Include *.cpp,*.h,*.hpp | Where-Object {
    -not (Test-IsAllowedPath $_.FullName)
} | ForEach-Object {
    $content = Get-Content $_.FullName -Raw -ErrorAction SilentlyContinue
    if ($content) {
        foreach ($pattern in $forbiddenIncludes) {
            if ($content -match "#include\s*[<`"]\s*$pattern\s*[>`"]") {
                $violations += [PSCustomObject]@{
                    File = $_.FullName.Replace($PWD, ".")
                    Line = ($content -split "`n").IndexOf(($content -split "`n" | Where-Object { $_ -match $pattern })) + 1
                    Category = "Include"
                    Issue = "Windows include: $pattern"
                }
            }
        }
    }
}

# Check for forbidden types in headers (outside Win32)
Write-Host "Checking for forbidden Windows types in headers..." -ForegroundColor Yellow
Get-ChildItem -Recurse -Include *.h,*.hpp | Where-Object {
    -not (Test-IsAllowedPath $_.FullName) -and (Test-IsHeaderFile $_.FullName)
} | ForEach-Object {
    $content = Get-Content $_.FullName -Raw -ErrorAction SilentlyContinue
    if ($content) {
        foreach ($type in $forbiddenTypes) {
            # Check for type usage (but not in comments)
            $lines = $content -split "`n"
            for ($i = 0; $i -lt $lines.Count; $i++) {
                $line = $lines[$i]
                # Skip comments
                if ($line -match "^\s*//" -or $line -match "/\*") { continue }
                if ($line -match "\b$type\b") {
                    $violations += [PSCustomObject]@{
                        File = $_.FullName.Replace($PWD, ".")
                        Line = $i + 1
                        Category = "Type"
                        Issue = "Windows type: $type"
                    }
                }
            }
        }
    }
}

# Check for forbidden macros in headers
Write-Host "Checking for forbidden Windows macros in headers..." -ForegroundColor Yellow
Get-ChildItem -Recurse -Include *.h,*.hpp | Where-Object {
    -not (Test-IsAllowedPath $_.FullName) -and (Test-IsHeaderFile $_.FullName)
} | ForEach-Object {
    $content = Get-Content $_.FullName -Raw -ErrorAction SilentlyContinue
    if ($content) {
        foreach ($macro in $forbiddenMacros) {
            $lines = $content -split "`n"
            for ($i = 0; $i -lt $lines.Count; $i++) {
                $line = $lines[$i]
                if ($line -match "^\s*//" -or $line -match "/\*") { continue }
                if ($line -match "\b$macro\b") {
                    $violations += [PSCustomObject]@{
                        File = $_.FullName.Replace($PWD, ".")
                        Line = $i + 1
                        Category = "Macro"
                        Issue = "Windows macro: $macro"
                    }
                }
            }
        }
    }
}

# Report results
Write-Host "`n" -NoNewline
if ($violations.Count -eq 0) {
    Write-Host "[OK] No platform leaks detected!" -ForegroundColor Green
    exit 0
} else {
    Write-Host "[FAIL] Found $($violations.Count) violation(s):" -ForegroundColor Red
    Write-Host ""
    $violations | Group-Object Category | ForEach-Object {
        Write-Host "  $($_.Name): $($_.Count) violation(s)" -ForegroundColor Yellow
    }
    Write-Host ""
    Write-Host "Details:" -ForegroundColor Yellow
    foreach ($v in $violations) {
        $msg = '  {0}:{1} - {2}' -f $v.File, $v.Line, $v.Issue
        Write-Host $msg -ForegroundColor Red
    }
    exit 1
}

