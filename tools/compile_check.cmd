@echo off
rem Compile one readable/src or readable/tests translation unit, for style agents
rem to check syntax without touching the shared CMake build tree.
rem
rem Usage: compile_check.cmd readable\src\patches.cpp
rem        set ZP_VCVARS32=<vcvars32.bat> to use a different toolchain path.
setlocal EnableExtensions
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI"
set "VC=%ZP_VCVARS32%"
if "%VC%"=="" set "VC=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat"
if not exist "%VC%" (
  echo error: vcvars32.bat not found; set ZP_VCVARS32 1>&2
  exit /b 2
)
call "%VC%" >nul || (echo error: vcvars32 failed 1>&2 & exit /b 2)
set "SRC=%ROOT%\readable\src"
set "OUT=%ROOT%\.parity-verify\build\style-check"
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++17 /O2 /EHsc /W3 /DUNICODE /D_UNICODE /DNDEBUG /DNOMINMAX %ZP_EXTRA% /I"%SRC%" /c "%~1" /Fo"%OUT%\%~n1.obj"
if errorlevel 1 exit /b 1
echo compiled: %~n1
endlocal
