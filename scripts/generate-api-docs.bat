@echo off
REM ========================================
REM NOMAD DAW - API Documentation Generator
REM ========================================

powershell.exe -ExecutionPolicy Bypass -File "%~dp0generate-api-docs.ps1" %*
