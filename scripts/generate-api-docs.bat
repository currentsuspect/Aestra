@echo off
REM ========================================
REM Aestra - API Documentation Generator
REM ========================================

powershell.exe -ExecutionPolicy Bypass -File "%~dp0generate-api-docs.ps1" %*
