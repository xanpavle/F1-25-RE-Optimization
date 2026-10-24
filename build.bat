@echo off
setlocal
rem Build the F1 25 RE-Optimization mod (proxy dinput8.dll) with mingw-w64.
set "GCC=C:\msys64\mingw64\bin\gcc.exe"
if not exist "%GCC%" set "GCC=gcc"
rem gcc needs its own runtime DLLs on PATH.
if exist "C:\msys64\mingw64\bin" set "PATH=C:\msys64\mingw64\bin;%PATH%"

set "SRC=%~dp0src"
set "OUT=%~dp0build"
if not exist "%OUT%" mkdir "%OUT%"

echo Building with: %GCC%
"%GCC%" -O2 -shared -static -static-libgcc ^
  -o "%OUT%\dinput8.dll" ^
  "%SRC%\proxy.cpp" "%SRC%\hook.cpp" "%SRC%\tune.cpp" "%SRC%\profile.cpp" "%SRC%\gpu.cpp" "%SRC%\scan.cpp" "%SRC%\enforce.cpp" ^
  -luser32 -lkernel32 -lshell32 -ladvapi32 -lgdi32

if errorlevel 1 (
  echo.
  echo BUILD FAILED
  exit /b 1
)
echo.
echo OK: %OUT%\dinput8.dll
echo Copy build\dinput8.dll and f125_tune.ini next to F1_25.exe.
