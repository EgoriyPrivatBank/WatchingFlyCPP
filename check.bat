@echo off
rem Run the regression check with Windows PowerShell 5.1.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0check.ps1" %*
exit /b %errorlevel%
