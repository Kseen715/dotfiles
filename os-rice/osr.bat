@echo off
rem os-rice\osr.bat -- thin cmd.exe launcher for osr.ps1 (the C core's real
rem Windows front end). Exists so `osr install ...` works from an ordinary
rem cmd.exe or a double-click, without anyone needing to know it's
rem PowerShell underneath -- osr.ps1 does the actual bootstrap + dispatch,
rem this just forwards every argument to it and relays its exit code.

where powershell >nul 2>&1
if errorlevel 1 (
    echo osr.bat: no 'powershell' found on PATH.
    echo This C-core tier's dispatcher needs PowerShell today ^(a fully
    echo PowerShell-free path is future work, tracked alongside the XP
    echo toolchain in PLAN_UNIVERSAL.md^). If the C core is already built,
    echo you can run it directly instead, e.g.: build\install.exe --list
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0osr.ps1" %*
exit /b %ERRORLEVEL%
