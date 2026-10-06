@echo off
rem cmk.cmd - Windows launcher for ./cmk (the CLI over the umbrella CMake build).
rem
rem Same commands as on Linux, from PowerShell or cmd:
rem   .\cmk list cosmo
rem   .\cmk build cosmo cosmo-cc
rem   .\cmk run interstellar -- clip.mp4
rem   .\cmk test -R artboard
rem   .\cmk --debug build cosmo
rem
rem It puts the MSYS2 MinGW64 toolchain (gcc, cmake, ninja, pkg-config, and the
rem GTK DLLs a built app needs at run time) first on PATH, then hands every
rem argument to the Python cmk. MSYS2 is looked for at %MSYS2_ROOT%, else C:\msys64.
setlocal

if not defined MSYS2_ROOT set "MSYS2_ROOT=C:\msys64"
set "MINGW_BIN=%MSYS2_ROOT%\mingw64\bin"
if not exist "%MINGW_BIN%\g++.exe" (
  echo cmk: no MinGW64 toolchain at "%MINGW_BIN%" 1>&2
  echo cmk: install MSYS2 ^(see DEVELOPMENT.md 2.1^) or set MSYS2_ROOT 1>&2
  exit /b 1
)
set "PATH=%MINGW_BIN%;%MSYS2_ROOT%\usr\bin;%PATH%"
set "MSYSTEM=MINGW64"

rem Prefer MinGW's own Python, so the interpreter matches the toolchain; fall
rem back to the py launcher, then whatever python is on PATH.
set "PY=python"
where py >nul 2>nul && set "PY=py -3"
if exist "%MINGW_BIN%\python3.exe" set "PY="%MINGW_BIN%\python3.exe""
%PY% "%~dp0cmk" %*
exit /b %ERRORLEVEL%
