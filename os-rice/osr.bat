@echo off
rem os-rice\osr.bat -- thin cmd.exe launcher for osr.ps1, which is itself a
rem thin launcher for build\osr.exe (the core). Exists so `osr install ...`
rem works from an ordinary cmd.exe or a double-click, without anyone needing
rem to know PowerShell is in the middle -- osr.ps1 does the bootstrap and the
rem dispatch, this forwards every argument to it and relays its exit code.
rem
rem PowerShell is used for the BOOTSTRAP only (finding gcc, running nob), not
rem for any of the work: the core is a compiled binary and calls no shell.

where powershell >nul 2>&1
if errorlevel 1 (
    echo osr.bat: no 'powershell' found on PATH.
    echo osr.ps1 needs it to bootstrap the build. If the core is already
    echo built, run it directly instead, e.g.: build\osr.exe install list-rices
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0osr.ps1" %*
exit /b %ERRORLEVEL%
