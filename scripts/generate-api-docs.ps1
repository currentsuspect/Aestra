#!/usr/bin/env pwsh
# ========================================
# 🧭 Aestra - API Documentation Generator
# ========================================
# Quick script to generate and view API docs
# ----------------------------------------

param(
    [Parameter(Mandatory = $false)]
    [ValidateSet("generate", "clean", "view", "stats", "help")]
    [string]$Action = "generate",
    
    [Parameter(Mandatory = $false)]
    [switch]$Open = $false,
    
    [Parameter(Mandatory = $false)]
    [switch]$Verbose = $false
)

# Colors for output
$ErrorColor = "Red"
$SuccessColor = "Green"
$InfoColor = "Cyan"
$WarningColor = "Yellow"

function Write-ColorOutput {
    param(
        [string]$Message,
        [string]$Color = "White"
    )
    Write-Host $Message -ForegroundColor $Color
}

function Show-Help {
    Write-ColorOutput "`n📚 Aestra API Documentation Generator`n" $InfoColor
    Write-ColorOutput "Usage: .\generate-api-docs.ps1 [Action] [-Open] [-Verbose]`n"
    Write-ColorOutput "Actions:" $InfoColor
    Write-ColorOutput "  generate  - Generate API documentation (default)" "White"
    Write-ColorOutput "  clean     - Remove generated documentation" "White"
    Write-ColorOutput "  view      - Open documentation in browser" "White"
    Write-ColorOutput "  stats     - Show documentation statistics" "White"
    Write-ColorOutput "  help      - Show this help message`n" "White"
    
    Write-ColorOutput "Flags:" $InfoColor
    Write-ColorOutput "  -Open     - Automatically open docs after generation" "White"
    Write-ColorOutput "  -Verbose  - Show detailed output`n" "White"
    
    Write-ColorOutput "Examples:" $InfoColor
    Write-ColorOutput "  .\generate-api-docs.ps1" "Gray"
    Write-ColorOutput "  .\generate-api-docs.ps1 generate -Open" "Gray"
    Write-ColorOutput "  .\generate-api-docs.ps1 clean" "Gray"
    Write-ColorOutput "  .\generate-api-docs.ps1 stats`n" "Gray"
}

function Test-DoxygenInstalled {
    try {
        $version = & doxygen --version 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-ColorOutput "✓ Doxygen found: $version" $SuccessColor
            return $true
        }
    }
    catch {
        Write-ColorOutput "✗ Doxygen not found!" $ErrorColor
        Write-ColorOutput "  Install with: choco install doxygen.install" $WarningColor
        Write-ColorOutput "  Or download from: https://www.doxygen.nl/download.html" $WarningColor
        return $false
    }
    return $false
}

function Test-GraphvizInstalled {
    try {
        $version = & dot -V 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-ColorOutput "✓ Graphviz found: $version" $SuccessColor
            return $true
        }
    }
    catch {
        Write-ColorOutput "⚠ Graphviz not found (optional - for diagrams)" $WarningColor
        Write-ColorOutput "  Install with: choco install graphviz" $InfoColor
        return $false
    }
    return $false
}

function Invoke-CleanDocs {
    Write-ColorOutput "`n🧹 Cleaning documentation...`n" $InfoColor
    
    $paths = @(
        "docs\api-reference\html",
        "docs\api-reference\xml",
        "docs\api-reference\latex",
        "doxygen_warnings.log",
        "doxygen_output.log"
    )
    
    foreach ($path in $paths) {
        if (Test-Path $path) {
            Remove-Item -Recurse -Force $path
            Write-ColorOutput "✓ Removed: $path" $SuccessColor
        }
    }
    
    Write-ColorOutput "`n✅ Cleanup complete!`n" $SuccessColor
}

function Invoke-GenerateDocs {
    Write-ColorOutput "`n📚 Generating API documentation...`n" $InfoColor
    
    # Check prerequisites
    if (-not (Test-DoxygenInstalled)) {
        return $false
    }
    
    Test-GraphvizInstalled | Out-Null
    
    # Check if Doxyfile exists
    if (-not (Test-Path "Doxyfile")) {
        Write-ColorOutput "✗ Doxyfile not found!" $ErrorColor
        Write-ColorOutput "  Make sure you're in the Aestra project root directory." $WarningColor
        return $false
    }
    
    # Generate documentation
    Write-ColorOutput "`nRunning Doxygen...`n" $InfoColor
    
    if ($Verbose) {
        & doxygen Doxyfile
    }
    else {
        & doxygen Doxyfile 2>&1 | Out-Null
    }
    
    if ($LASTEXITCODE -ne 0) {
        Write-ColorOutput "`n✗ Documentation generation failed!`n" $ErrorColor
        return $false
    }
    
    Write-ColorOutput "`n✅ Documentation generated successfully!`n" $SuccessColor
    
    # Show warnings if any
    if (Test-Path "doxygen_warnings.log") {
        $warnings = Get-Content "doxygen_warnings.log"
        $warningCount = $warnings.Count
        
        if ($warningCount -gt 0) {
            Write-ColorOutput "⚠ Found $warningCount documentation warnings" $WarningColor
            Write-ColorOutput "  Review: doxygen_warnings.log`n" $InfoColor
            
            if ($Verbose) {
                Write-ColorOutput "First 10 warnings:" $WarningColor
                $warnings | Select-Object -First 10 | ForEach-Object {
                    Write-ColorOutput "  $_" "Gray"
                }
            }
        }
        else {
            Write-ColorOutput "✓ No documentation warnings!`n" $SuccessColor
        }
    }
    
    # Show output location
    Write-ColorOutput "📁 Documentation location:" $InfoColor
    Write-ColorOutput "  docs\api-reference\html\index.html`n" "White"
    
    return $true
}

function Invoke-ViewDocs {
    $indexPath = "docs\api-reference\html\index.html"
    
    if (-not (Test-Path $indexPath)) {
        Write-ColorOutput "✗ Documentation not found!" $ErrorColor
        Write-ColorOutput "  Generate it first with: .\generate-api-docs.ps1 generate`n" $WarningColor
        return
    }
    
    Write-ColorOutput "`n🌐 Opening documentation in browser...`n" $InfoColor
    Start-Process $indexPath
}

function Show-Stats {
    Write-ColorOutput "`n📊 Documentation Statistics`n" $InfoColor
    
    if (-not (Test-Path "docs\api-reference\html")) {
        Write-ColorOutput "✗ Documentation not generated yet!" $ErrorColor
        Write-ColorOutput "  Generate it first with: .\generate-api-docs.ps1 generate`n" $WarningColor
        return
    }
    
    # Count HTML files by type
    $htmlDir = "docs\api-reference\html"
    
    $classFiles = @(Get-ChildItem -Path $htmlDir -Filter "class*.html" -File).Count
    $structFiles = @(Get-ChildItem -Path $htmlDir -Filter "struct*.html" -File).Count
    $namespaceFiles = @(Get-ChildItem -Path $htmlDir -Filter "namespace*.html" -File).Count
    
    Write-ColorOutput "📦 Generated Items:" $InfoColor
    Write-ColorOutput "  Classes:    $classFiles" "White"
    Write-ColorOutput "  Structs:    $structFiles" "White"
    Write-ColorOutput "  Namespaces: $namespaceFiles`n" "White"
    
    # Check warnings
    if (Test-Path "doxygen_warnings.log") {
        $warnings = Get-Content "doxygen_warnings.log"
        $totalWarnings = $warnings.Count
        $undocumented = ($warnings | Select-String "undocumented").Count
        $paramIssues = ($warnings | Select-String "param").Count
        
        Write-ColorOutput "⚠ Documentation Issues:" $WarningColor
        Write-ColorOutput "  Total warnings:       $totalWarnings" "White"
        Write-ColorOutput "  Undocumented items:   $undocumented" "White"
        Write-ColorOutput "  Parameter issues:     $paramIssues`n" "White"
        
        # Quality assessment
        if ($undocumented -lt 50) {
            $quality = "Excellent ✨"
            $color = $SuccessColor
        }
        elseif ($undocumented -lt 100) {
            $quality = "Good ✓"
            $color = $InfoColor
        }
        else {
            $quality = "Needs Improvement ⚠"
            $color = $WarningColor
        }
        
        Write-ColorOutput "📈 Documentation Quality: $quality`n" $color
    }
    
    # File size
    $htmlSize = (Get-ChildItem -Path $htmlDir -Recurse | Measure-Object -Property Length -Sum).Sum
    $sizeMB = [math]::Round($htmlSize / 1MB, 2)
    Write-ColorOutput "💾 Total size: $sizeMB MB`n" $InfoColor
}

# Main execution
switch ($Action) {
    "help" {
        Show-Help
    }
    "clean" {
        Invoke-CleanDocs
    }
    "generate" {
        $success = Invoke-GenerateDocs
        if ($success -and $Open) {
            Invoke-ViewDocs
        }
    }
    "view" {
        Invoke-ViewDocs
    }
    "stats" {
        Show-Stats
    }
    default {
        Show-Help
    }
}
