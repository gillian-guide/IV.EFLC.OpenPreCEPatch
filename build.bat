@echo off
setlocal EnableExtensions
pushd "%~dp0"

rem Build both shipped ASIs (OpenPreCEPatch + XLivelessAddon) into bin\.
rem Usage: build.bat [--test]

set "BUILD_DIR=build"
set "DIST_DIR=bin"
set "DO_TEST=0"
if /I "%~1"=="--test" set "DO_TEST=1"
if /I "%~1"=="test" set "DO_TEST=1"

where cmake >nul 2>nul
if errorlevel 1 (
    echo [build] CMake was not found in PATH.
    popd
    exit /b 1
)

if exist "%BUILD_DIR%\CMakeCache.txt" (
    findstr /C:"CMAKE_GENERATOR_PLATFORM:INTERNAL=Win32" "%BUILD_DIR%\CMakeCache.txt" >nul 2>nul
    if errorlevel 1 (
        echo [build] "%BUILD_DIR%" is not configured for Win32.
        echo [build] Use a different -B directory or remove the cache, then rerun.
        popd
        exit /b 1
    )
)

echo [build] Configuring 32-bit Release in "%BUILD_DIR%"...
cmake -S . -B "%BUILD_DIR%" -A Win32
if errorlevel 1 goto :fail

echo [build] Building both modules...
cmake --build "%BUILD_DIR%" --config Release -- /m
if errorlevel 1 goto :fail

if "%DO_TEST%"=="1" (
    echo [build] Running native tests...
    ctest --test-dir "%BUILD_DIR%" -C Release --output-on-failure
    if errorlevel 1 goto :fail
)

echo.
echo [build] Output folder: "%DIST_DIR%"
set "MISSING="
for %%F in ("%DIST_DIR%\IV.EFLC.OpenPreCEPatch.asi" "%DIST_DIR%\IV.EFLC.OpenPreCEPatch.ini" "%DIST_DIR%\XLivelessAddon.asi" "%DIST_DIR%\XLivelessAddon.ini") do (
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
