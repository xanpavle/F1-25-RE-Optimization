@echo off
setlocal
set "GAME=C:\Program Files (x86)\Steam\steamapps\common\F1 25"

del /Q "%GAME%\dinput8.dll" >nul 2>&1
if errorlevel 1 (
  echo Could not delete "%GAME%\dinput8.dll".
  echo Right-click and "Run as administrator", then retry.
  pause
  exit /b 1
)
del /Q "%GAME%\f125_tune.ini" >nul 2>&1
echo Removed dinput8.dll and f125_tune.ini from:
echo   %GAME%
echo (f125_tune.log was left in place.)
pause
