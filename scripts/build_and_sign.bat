@echo off
setlocal enabledelayedexpansion

echo =============================================
echo NOMAD DAW Build and Sign Tool
echo =============================================

:: Check if we're running as administrator (optional, for certificate generation)
net session >nul 2>&1
if %ERRORLEVEL% == 0 (
    echo Running with administrative privileges
) else (
    echo Warning: Not running as administrator - certificate generation may fail
    echo Continuing anyway...
)

:: Generate a self-signed certificate if it doesn't exist in the store
powershell -Command "$cert = Get-ChildItem -Path Cert:\CurrentUser\My | Where-Object {$_.Subject -eq 'CN=Nomad Studios Dev Cert'}; if (-not $cert) { New-SelfSignedCertificate -Subject 'CN=Nomad Studios Dev Cert' -CertStoreLocation 'Cert:\CurrentUser\My' -KeyAlgorithm RSA -KeyLength 2048 -HashAlgorithm SHA256 -Type CodeSigningCert; Write-Host 'Certificate generated successfully' } else { Write-Host 'Using existing certificate' }"
if %ERRORLEVEL% NEQ 0 (
    echo Failed to generate or find certificate
    pause
    exit /b 1
)

:: Build the project
echo.
echo Building NOMAD DAW...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"
if %ERRORLEVEL% NEQ 0 (
    echo Build failed
    pause
    exit /b 1
)

:: Sign the executable
echo.
echo Signing the executable...
signtool sign /a /v /n "Nomad Studios Dev Cert" /tr http://timestamp.digicert.com /td sha256 "build\Release\NomadDAW.exe"
if %ERRORLEVEL% NEQ 0 (
    echo Signing failed
    pause
    exit /b 1
)

echo.
echo Build and signing completed successfully!
pause
