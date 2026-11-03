@echo off
setlocal EnableExtensions
pushd "%~dp0"

rem Build the shipped 32-bit ASI into bin\.
rem Usage: build.bat [--test]

set "DO_TEST=0"
if /I "%~1"=="--test" set "DO_TEST=1"
if /I "%~1"=="test" set "DO_TEST=1"

where cmake >nul 2>nul
if errorlevel 1 (
    echo [build] CMake was not found in PATH.
    popd
    exit /b 1
)

if not exist "external\injector\include\injector\injector.hpp" (
    echo [build] Vendor submodules are missing.
    echo [build] Run: git submodule update --init --recursive
    popd
    exit /b 1
)

echo [build] Configuring 32-bit Release...
cmake --preset x86
if errorlevel 1 goto :fail

echo [build] Building...
cmake --build --preset x86-release
if errorlevel 1 goto :fail

if "%DO_TEST%"=="1" (
    echo [build] Running native tests...
    ctest --preset x86-release
    if errorlevel 1 goto :fail
)

echo.
echo [build] Output folder: "%CD%\bin"
set "MISSING="
for %%F in ("bin\IV.EFLC.OpenPreCEPatch.asi" "bin\IV.EFLC.OpenPreCEPatch.ini") do (
    if exist "%%~F" (
        echo   %%~nxF
    ) else (
        echo   MISSING: %%~nxF
        set "MISSING=1"
    )
)
if defined MISSING goto :fail

echo [build] Done.
popd
exit /b 0

:fail
echo [build] FAILED.
popd
exit /b 1
