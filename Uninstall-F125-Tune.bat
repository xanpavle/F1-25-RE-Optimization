@echo off
setlocal
rem Removes the mod from the game folder. Leaves your graphics settings alone.
rem Detects the game install the same way the installer does.
set "PS=powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-F125-Tune.ps1""

if "%~1"=="" (
  %PS% -Action Uninstall
) else (
  %PS% -Action Uninstall -Game "%~1"
)

if errorlevel 1 (
  echo.
  pause
)
