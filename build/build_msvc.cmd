@echo off
rem Build the 32-bit ZolikaPatch.asi with the Visual Studio x86 toolchain.
rem Run this from a "Developer Command Prompt for VS" (x86), or pass the path
rem to vcvars32.bat as the first argument.
rem
rem Usage: build_msvc.cmd [vcvars32.bat] [outdir]
rem
rem The sources live under ..\src (the layout follows the original mod's tree:
rem loader, installers and hook bodies in their own folders); the two vendored
rem libraries are git submodules under ..\external.
setlocal EnableExtensions
if not "%~1"=="" (
  call "%~1" >nul || (echo error: vcvars32 failed 1>&2 & exit /b 2)
)
where cl >nul 2>nul || (
  echo error: cl.exe not on PATH; run from an x86 Developer Command Prompt 1>&2
  exit /b 2
)
set "ROOT=%~dp0.."
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "SRC=%ROOT%\src"
set "EXTDIR=%ROOT%\external"
set "OUT=%~2"
if "%OUT%"=="" set "OUT=%ROOT%\build\msvc-x86"
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OUT%\obj" mkdir "%OUT%\obj"
if not exist "%EXTDIR%\injector\include\injector" (
  echo error: submodules not checked out; run: git submodule update --init --recursive 1>&2
  exit /b 2
)
set "COMMON=/nologo /std:c++17 /O2 /EHsc /W3 /DUNICODE /D_UNICODE /DNDEBUG /DNOMINMAX /I"%SRC%" /I"%EXTDIR%\injector\include" /I"%EXTDIR%\Hooking.Patterns""
set "LDFLAGS=/link /MACHINE:X86 /SUBSYSTEM:WINDOWS /BASE:0x10000000 /INCREMENTAL:NO /DYNAMICBASE /NOLOGO"

cl %COMMON% ^
  "%SRC%\dllmain.cpp" ^
  "%EXTDIR%\Hooking.Patterns\Hooking.Patterns.cpp" ^
  "%SRC%\catalogue_data.cpp" ^
  "%SRC%\patch_data.cpp" ^
  "%SRC%\xlive\xliveless.cpp" ^
  "%SRC%\pch.cpp" ^
  /LD /Fe:"%OUT%\ZolikaPatch.asi" /Fo"%OUT%\obj\\" %LDFLAGS% ^
  /DEF:"%ROOT%\ZolikaPatch.def" version.lib user32.lib shlwapi.lib
if errorlevel 1 exit /b 1

echo built:
dir /b "%OUT%\*.asi"
endlocal
