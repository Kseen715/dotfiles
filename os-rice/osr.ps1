# os-rice/osr.ps1 -- Windows front end for the C core (install.c/lib/*.c),
# mirrors ./osr (the POSIX dispatcher: install/switch/theme/wallpaper/
# module/list/themes/modules/test) scaled to what the C core actually
# implements so far -- see install.c's own header comment and
# PLAN_UNIVERSAL.md for the exact scope line. This is now the ONE Windows
# rice entry point for every Windows version this repo supports (10/11
# today, XP/Win7 once Phase 0/2 land) -- the former windows-rice/ tree
# (plain PowerShell, no C core underneath) has been retired and fully
# ingested here, see PLAN_UNIVERSAL.md decision 8.
#
#   osr.ps1 install <rice> [-Theme <name>]
#                                        install a rice
#   osr.ps1 switch  <rice> [-Theme <name>]
#                                        same engine as install -- the C core
#                                        has no separate switch/install path
#                                        (matches install.sh's own "switch and
#                                        install share one idempotent engine")
#   osr.ps1 theme   [<name>]            with a name: re-theme what's already
#                                        installed, no packages (hotkey-safe).
#                                        With no name: print the current theme.
#   osr.ps1 wallpaper [<path>|-List|-Next]
#                                        show/list/step/set the wallpaper
#   osr.ps1 module <name>... [-Theme <name>]
#                                        install module(s) directly, no rice
#   osr.ps1 list                        list available rices
#   osr.ps1 modules                     list available modules
#   osr.ps1 test                        build + run the C unit tests
#
# Bootstraps its own build tool on first use: if build\nob.exe doesn't exist,
# compiles it from nob.c with whatever gcc is on PATH (nob.c/nob.h are the
# build system now, replacing the old Makefile -- see PLAN_UNIVERSAL.md
# decision 6). nob.exe then builds install.exe/wallpaper.exe on demand the
# same way, so a first run here is the only time a build command runs by
# hand at all. Every one of those binaries lives in build\, never next to
# the sources -- see nob.c's BUILD_DIR.
#
# Requires PowerShell, so this is the Win7/10/11 entry point. osr.bat next
# to it is a thin cmd.exe launcher for this file, for anyone who'd rather
# type `osr` from an ordinary cmd.exe / double-click.

param(
    [Parameter(Position = 0)]
    [string]$Command = "",

    [Parameter(Position = 1, ValueFromRemainingArguments = $true)]
    [string[]]$Rest = @()
)

$ScriptDir = $PSScriptRoot
Set-Location $ScriptDir

function Info([string]$msg) { Write-Host $msg -ForegroundColor Cyan }
function ErrMsg([string]$msg) { Write-Host $msg -ForegroundColor Red }

function Show-Usage {
    Write-Host "Usage:"
    Write-Host "  osr.ps1 install <rice> [-Theme <name>]"
    Write-Host "  osr.ps1 switch  <rice> [-Theme <name>]  (same engine as install)"
    Write-Host "  osr.ps1 theme   [<name>]         re-theme (a name) or print current (none)"
    Write-Host "  osr.ps1 wallpaper [<path>|-List|-Next]"
    Write-Host "  osr.ps1 module <name>... [-Theme <name>]"
    Write-Host "  osr.ps1 list                     list available rices"
    Write-Host "  osr.ps1 modules                  list available modules"
    Write-Host "  osr.ps1 test                     build + run the C unit tests"
    Write-Host ""
    Write-Host "modules.c knows how to fully install+theme fastfetch, wezterm, pwsh, and"
    Write-Host "oh-my-posh; every other rice.list entry is package-only. See install.c and"
    Write-Host "PLAN_UNIVERSAL.md for the exact scope line."
}

# Ensure-Nob -- compile build\nob.exe from nob.c if it doesn't exist yet
# (creating build\ first: it is the one directory the bootstrap cannot ask
# nob to make for it). Returns $true if nob.exe is ready to run, $false
# (with a message already printed) if there's no gcc to bootstrap it with.
function Ensure-Nob {
    $buildDir = Join-Path $ScriptDir "build"
    if (Test-Path (Join-Path $buildDir "nob.exe")) { return $true }
    $gcc = Get-Command gcc -ErrorAction SilentlyContinue
    if (-not $gcc) {
        ErrMsg "build\nob.exe not found and no gcc on PATH -- install a mingw-w64 gcc (e.g. 'scoop install gcc') and try again"
        return $false
    }
    Info "build\nob.exe not found -- bootstrapping it with gcc..."
    if (-not (Test-Path $buildDir)) { New-Item -ItemType Directory -Path $buildDir | Out-Null }
    & gcc -o build\nob.exe nob.c
    return $LASTEXITCODE -eq 0
}

# Ensure-Binaries -- make sure build\install.exe and build\wallpaper.exe are
# built, via nob.exe's default target (which builds both -- see nob.c's
# build_all).
function Ensure-Binaries {
    if (-not (Ensure-Nob)) { return $false }
    & .\build\nob.exe
    return $LASTEXITCODE -eq 0
}

# current theme, read straight from the state file -- same file the
# install/wallpaper binaries write to, no need to shell out for a read.
function Get-CurrentTheme {
    $statePath = Join-Path $env:USERPROFILE ".config\osr\state"
    if (-not (Test-Path $statePath)) { return $null }
    $line = Get-Content $statePath | Where-Object { $_ -like "theme=*" } | Select-Object -Last 1
    if (-not $line) { return $null }
    return $line.Substring(6)
}

switch ($Command) {
    { $_ -in @("install", "switch") } {
        if (-not (Ensure-Binaries)) { exit 1 }
        if ($Rest.Count -eq 0 -or $Rest[0].StartsWith("-")) {
            ErrMsg "osr.ps1 $Command <rice> [-Theme <name>]"
            exit 1
        }
        $rice = $Rest[0]
        $flags = @($rice)
        for ($i = 1; $i -lt $Rest.Count; $i++) {
            if ($Rest[$i] -eq "-Theme" -and $i + 1 -lt $Rest.Count) { $flags += @("--theme", $Rest[$i + 1]); $i++ }
        }
        & .\build\install.exe @flags
        exit $LASTEXITCODE
    }
    "theme" {
        if ($Rest.Count -eq 0) {
            $cur = Get-CurrentTheme
            Write-Host ($(if ($cur) { $cur } else { "(none applied)" }))
            exit 0
        }
        if (-not (Ensure-Binaries)) { exit 1 }
        & .\build\install.exe --theme-only --theme $Rest[0]
        exit $LASTEXITCODE
    }
    "wallpaper" {
        if (-not (Ensure-Binaries)) { exit 1 }
        $flags = @()
        foreach ($a in $Rest) {
            if ($a -eq "-List") { $flags += "--list" }
            elseif ($a -eq "-Next") { $flags += "--next" }
            else { $flags += $a }
        }
        & .\build\wallpaper.exe @flags
        exit $LASTEXITCODE
    }
    "module" {
        if (-not (Ensure-Binaries)) { exit 1 }
        if ($Rest.Count -eq 0) {
            ErrMsg "osr.ps1 module <name>... [-Theme <name>]"
            exit 1
        }
        $names = @()
        $flags = @("--module")
        for ($i = 0; $i -lt $Rest.Count; $i++) {
            if ($Rest[$i] -eq "-Theme" -and $i + 1 -lt $Rest.Count) { $flags += @("--theme", $Rest[$i + 1]); $i++ }
            else { $names += $Rest[$i] }
        }
        & .\build\install.exe @flags @names
        exit $LASTEXITCODE
    }
    "list" {
        if (-not (Ensure-Binaries)) { exit 1 }
        & .\build\install.exe --list
        exit $LASTEXITCODE
    }
    "modules" {
        if (-not (Ensure-Binaries)) { exit 1 }
        & .\build\install.exe --list-modules
        exit $LASTEXITCODE
    }
    "test" {
        if (-not (Ensure-Nob)) { exit 1 }
        & .\build\nob.exe test
        exit $LASTEXITCODE
    }
    "" {
        Show-Usage
        exit 0
    }
    { $_ -eq "-h" -or $_ -eq "--help" } {
        Show-Usage
        exit 0
    }
    default {
        ErrMsg "osr.ps1: unknown command '$Command' (try: install, switch, theme, wallpaper, module, list, modules, test)"
        exit 1
    }
}
