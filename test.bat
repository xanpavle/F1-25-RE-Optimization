@echo off
setlocal
rem Build and run every self-test. No game install or admin rights required.
set "GCC=C:\msys64\mingw64\bin\gcc.exe"
if not exist "%GCC%" set "GCC=gcc"
set "SRC=%~dp0src"
set "TST=%~dp0test"
set "OUT=%~dp0build"
if not exist "%OUT%" mkdir "%OUT%"

echo == building tests ==
"%GCC%" -O2 -I "%SRC%" -o "%OUT%\hooktest.exe" "%TST%\hooktest.cpp" "%SRC%\hook.cpp" -static -static-libgcc || goto :fail
"%GCC%" -O2 -I "%SRC%" -o "%OUT%\tunetest.exe" "%TST%\tunetest.cpp" "%SRC%\tune.cpp" "%SRC%\profile.cpp" "%SRC%\gpu.cpp" -static -static-libgcc -luser32 -lshell32 -ladvapi32 || goto :fail
"%GCC%" -O2 -I "%SRC%" -o "%OUT%\profiletest.exe" "%TST%\profiletest.cpp" "%SRC%\profile.cpp" -static -static-libgcc || goto :fail
"%GCC%" -O2 -I "%SRC%" -o "%OUT%\gputest.exe" "%TST%\gputest.cpp" "%SRC%\gpu.cpp" -static -static-libgcc -luser32 -ladvapi32 || goto :fail

copy /Y "%~dp0f125_tune.ini" "%OUT%\f125_tune.ini" >nul

echo.
echo == hooktest ==
"%OUT%\hooktest.exe" || goto :fail

echo.
echo == tunetest ==
"%OUT%\tunetest.exe" || goto :fail

echo.
echo == gputest ==
"%OUT%\gputest.exe"
echo.
echo == profiletest: upscaler matrix ==
for %%M in (keep dlss fsr3 xess taa off) do (
  "%OUT%\profiletest.exe" "%TST%\sample_settings.xml" "%OUT%\sample_%%M.xml" %%M || goto :fail
  findstr /C:"<antialiasing" "%OUT%\sample_%%M.xml"
)

echo.
echo == ALL TESTS PASSED ==
exit /b 0

:fail
echo.
echo == TEST FAILED ==
exit /b 1
