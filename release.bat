@echo off
rem Package the built ASI, its INI and the licence/credits into release\.
setlocal
set "ROOT=%~dp0"
set "OUT=%ROOT%release"

if not exist "%ROOT%bin\IV.EFLC.OpenPreCEPatch.asi" (
    echo [release] bin\IV.EFLC.OpenPreCEPatch.asi is missing; run build.bat first.
    exit /b 1
)

if not exist "%OUT%" mkdir "%OUT%"
copy /y "%ROOT%bin\IV.EFLC.OpenPreCEPatch.asi" "%OUT%\" >nul
copy /y "%ROOT%data\plugins\IV.EFLC.OpenPreCEPatch.ini" "%OUT%\" >nul
copy /y "%ROOT%readme.md" "%OUT%\readme.md" >nul
copy /y "%ROOT%LICENSE" "%OUT%\LICENSE" >nul
copy /y "%ROOT%CREDITS.md" "%OUT%\CREDITS.md" >nul
copy /y "%ROOT%NOTICE" "%OUT%\NOTICE" >nul

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "Compress-Archive -Path '%OUT%\*' -DestinationPath '%ROOT%OpenPreCEPatch.zip' -Force"
echo [release] packed %OUT% and OpenPreCEPatch.zip
endlocal
