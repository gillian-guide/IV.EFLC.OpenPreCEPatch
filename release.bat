@echo off
rem Package the built ASI, the optional XLivelessAddon name shim and the INI.
setlocal
set "ROOT=%~dp0"
set "OUT=%ROOT%release"
if not exist "%OUT%" mkdir "%OUT%"
copy /y "%ROOT%bin\IV.EFLC.OpenPreCEPatch.asi" "%OUT%\"
copy /y "%ROOT%bin\XLivelessAddon.asi" "%OUT%\"
copy /y "%ROOT%data\plugins\IV.EFLC.OpenPreCEPatch.ini" "%OUT%\"
echo packed %OUT%
endlocal
