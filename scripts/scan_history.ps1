<#
Lightweight local scanner: runs gitleaks (if available) and lists largest git objects to help find large model blobs.

Usage: .\scripts\scan_history.ps1
#>
param()

Write-Host "Running local repo scans..." -ForegroundColor Cyan

# 1) gitleaks (if available)
if (Get-Command gitleaks -ErrorAction SilentlyContinue) {
    Write-Host "Running gitleaks detect --source . --report-path gitleaks-report.json" -ForegroundColor Yellow
    gitleaks detect --source . --report-path gitleaks-report.json
    if (Test-Path gitleaks-report.json) {
        Write-Host "gitleaks report saved to gitleaks-report.json" -ForegroundColor Green
    }
} else {
    Write-Host "gitleaks not found. Install it: https://github.com/zricethezav/gitleaks" -ForegroundColor Yellow
}

# 2) show largest git objects (requires git)
Write-Host "Listing largest git objects (top 50)" -ForegroundColor Yellow
git rev-list --objects --all | 
    git cat-file --batch-check='%(objecttype) %(objectname) %(rest) %(objectsize)' 2>$null | 
    awk '$1 == "blob" {print $0}' | sort -k4 -n | tail -n 50

Write-Host "Scan complete. If you see sensitive files, DO NOT push the public repo until history is cleaned." -ForegroundColor Cyan
