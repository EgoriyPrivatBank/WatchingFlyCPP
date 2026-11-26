@echo off
rem Build flysim.exe with the portable MinGW toolchain from ..\tools.
rem Usage: build.bat [release|debug|portable]
rem   release (default) - -march=native, bit-identical to the golden line.
rem   debug             - -O0 -g for debugging.
rem   portable          - no -march=native -> flysim_portable.exe (other CPUs;
rem                       same 20 s CSV, last printed digit may differ on 400 s).
setlocal
set "ROOT=%~dp0"
set "GXX=%ROOT%..\tools\mingw64\bin\g++.exe"
if exist "%GXX%" goto :toolchain_ok
where g++ >nul 2>nul
if errorlevel 1 (
  echo ERROR: g++ not found in "%GXX%" or PATH
  exit /b 1
)
set "GXX=g++"
:toolchain_ok
cd /d "%ROOT%"
if not exist src\connectome.cpp (
  echo ERROR: src\connectome.cpp not found next to build.bat
  exit /b 1
)
set "OPT=-O2"
set "ARCH=-march=native"
set "OUT=flysim.exe"
if "%1"=="debug" set "OPT=-O0 -g"
if "%1"=="portable" set "ARCH=" & set "OUT=flysim_portable.exe"
"%GXX%" -std=c++17 %OPT% %ARCH% -pthread -static -static-libgcc -static-libstdc++ -Wall -Wextra ^
  -o "%OUT%" src\connectome.cpp src\sim.cpp src\selftest.cpp src\flyeye.cpp src\eyetest.cpp src\metrics.cpp src\video.cpp src\live.cpp src\run.cpp src\report.cpp src\main.cpp -lws2_32 -lwinmm
if errorlevel 1 ( echo BUILD FAILED & exit /b 1 )
echo BUILD OK: %OUT%
