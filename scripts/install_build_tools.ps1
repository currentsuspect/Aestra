# Script to install Visual Studio 2022 Build Tools for Aestra Development
# Installs the minimal "Desktop development with C++" workload required for compiling native C++ apps.

$InstallerUrl = "https://aka.ms/vs/17/release/vs_BuildTools.exe"
$InstallerPath = "$env:TEMP\vs_BuildTools.exe"

Write-Host "Downloading Visual Studio Build Tools..." -ForegroundColor Cyan
Invoke-WebRequest -Uri $InstallerUrl -OutFile $InstallerPath

Write-Host "Download complete: $InstallerPath" -ForegroundColor Green
Write-Host "Starting Installer..." -ForegroundColor Cyan
Write-Host "This will install: Microsoft.VisualStudio.Workload.VCTools (C++ Build Tools)" -ForegroundColor Yellow
Write-Host "Please approve the User Account Control (UAC) prompt if it appears." -ForegroundColor Yellow

# Start the installer with arguments to select the C++ workload
# --passive: Shows progress but doesn't wait for user input (unless error)
# --wait: Script waits for installer to finish
# --norestart: Don't restart automatically
# --add Microsoft.VisualStudio.Workload.VCTools: The key component we need
# --includeRecommended: Adds Windows SDK and common tools

$ProcessIds = Start-Process -FilePath $InstallerPath -ArgumentList "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive --wait --norestart" -PassThru

Write-Host "Installer is running. Please wait for it to complete..." -ForegroundColor Cyan
$ProcessIds.WaitForExit()

if ($ProcessIds.ExitCode -eq 0) {
    Write-Host "Installation completed successfully!" -ForegroundColor Green
    Write-Host "You may need to restart your terminal (or computer) for the path changes to take effect." -ForegroundColor Green
}
elseif ($ProcessIds.ExitCode -eq 3010) {
    Write-Host "Installation completed, but a reboot is required." -ForegroundColor Yellow
}
else {
    Write-Host "Installation failed with exit code: $($ProcessIds.ExitCode)" -ForegroundColor Red
}
