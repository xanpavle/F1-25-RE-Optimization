@echo off
setlocal
rem Installs the F1 25 RE-Optimization mod (proxy dinput8.dll).
rem Auto-detects Steam / Epic / EA App installs. If detection fails, pass the
rem game folder:  Install-F125-Tune.bat "D:\Games\F1 25"
set "PS=powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-F125-Tune.ps1""

if "%~1"=="" (
  %PS% -Action Install
) else (
  %PS% -Action Install -Game "%~1"
)

if errorlevel 1 (
  echo.
  pause
)
