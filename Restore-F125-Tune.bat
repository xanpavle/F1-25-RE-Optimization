@echo off
setlocal
rem Removes the mod and restores stock graphics settings.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Restore-F125-Tune.ps1" %*
if errorlevel 1 pause
