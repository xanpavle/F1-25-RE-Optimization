@echo off
setlocal
rem ---- EDIT THIS if your game is installed elsewhere ----------------------
set "GAME=C:\Program Files (x86)\Steam\steamapps\common\F1 25"
rem -------------------------------------------------------------------------

if not exist "%GAME%\F1_25.exe" (
  echo.
  echo ERROR: F1_25.exe not found in:
  echo   %GAME%
  echo Edit the GAME= line at the top of this file and run it again.
  echo.
  pause
  exit /b 1
)

if not exist "%~dp0build\dinput8.dll" (
  echo ERROR: build\dinput8.dll missing. Run build.bat first.
  pause
  exit /b 1
)

echo Installing F1 25 Renderer Tune into:
echo   %GAME%
echo.

copy /Y "%~dp0build\dinput8.dll" "%GAME%\dinput8.dll" >nul
if errorlevel 1 (
  echo ERROR: could not write to the game folder.
  echo Right-click this file and "Run as administrator", then retry.
  pause
  exit /b 1
)
copy /Y "%~dp0f125_tune.ini" "%GAME%\f125_tune.ini" >nul

echo Installed:
echo   %GAME%\dinput8.dll
echo   %GAME%\f125_tune.ini
echo.
echo   1) Launch F1 25 OFFLINE (single-player) once.
echo   2) Quit, then open %GAME%\f125_tune.log to see what the engine read.
echo   3) Tune f125_tune.ini (set [log] all = 0 when done).
echo.
echo NOTE: online / anti-cheat modes are not supported. Remove the DLL there.
pause
