@echo off
setlocal enabledelayedexpansion

echo =============================================
echo NOMAD DAW Build and Sign Tool
echo =============================================

:: Check if we're running as administrator
net session >nul 2>&1
if %ERRORLEVEL% == 0 (
    echo Running with administrative privileges
) else (
    echo Please run this script as Administrator
    pause
    exit /b 1
)

:: Generate a self-signed certificate if it doesn't exist
if not exist "NomadCert.cer" (
    echo Generating new self-signed certificate...
    makecert -r -pe -n "CN=Nomad Studios Dev Cert" -ss My -sr CurrentUser -a sha256 -len 2048 -sky signature NomadCert.cer
    if %ERRORLEVEL% NEQ 0 (
        echo Failed to generate certificate
        pause
        exit /b 1
    )
    echo Certificate generated successfully
) else (
    echo Using existing certificate
)

:: Build the project
echo.
echo Building NOMAD DAW...
call build.ps1
if %ERRORLEVEL% NEQ 0 (
    echo Build failed
    pause
    exit /b 1
)

:: Sign the executable
echo.
echo Signing the executable...
signtool sign /a /v /n "Nomad Studios Dev Cert" /t http://timestamp.digicert.com "build\Release\NomadDAW.exe"
if %ERRORLEVEL% NEQ 0 (
    echo Signing failed
    pause
    exit /b 1
)

echo.
echo Build and signing completed successfully!
pause
