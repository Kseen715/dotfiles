# os-rice/osr.ps1 -- the Windows front end. Mirror of ./osr, the POSIX one,
# and deliberately as thin: bootstrap the build tool if it is missing, build
# build\osr.exe if it is stale, then hand the command straight to it.
#
#   osr.ps1 install <rice> [-Theme <name>]
#                                     install a rice
#   osr.ps1 switch  <rice> [-Theme <name>]
#                                     same engine as install -- there is no
#                                     separate switch path, and there is not
#                                     one on the POSIX side either: switch and
#                                     install share one idempotent engine
#   osr.ps1 theme   [<name>]          with a name: re-theme what is already
#                                     installed, no packages (hotkey-safe).
#                                     With none: print the current theme.
#   osr.ps1 wallpaper [<path>|-List|-Next]
#                                     show/list/step/set the wallpaper
#   osr.ps1 module <name>... [-Theme <name>]
#                                     install module(s) directly, no rice
#   osr.ps1 list                      list available rices
#   osr.ps1 themes                    list available themes
#   osr.ps1 modules                   list available modules
#   osr.ps1 test                      build + run the unit tests
#   osr.ps1 <anything else>           passed through to build\osr.exe, so every
#                                     core command (detect, pkg, build, state,
#                                     config, ...) is reachable by name
#
# WHY THIS IS SHORT NOW. It used to hold a command table of its own, because
# what it drove was install.exe -- a separate program with a separate option
# set. There is one core now, build\osr.exe, with the same command words the
# POSIX ./osr uses, so this file's whole job is the two things a launcher can
# do that a compiled binary cannot: notice it has not been built yet, and
# build itself.
#
# Bootstraps its own build tool on first use: if build\nob.exe does not exist,
# it is compiled from nob.c with whatever gcc is on PATH. nob.exe then builds
# osr.exe on demand, so a first run here is the only time a build command runs
# by hand at all. Everything lands in build\, never next to the sources -- see
# nob.c's BUILD_DIR.
#
# osr.bat next to this is a thin cmd.exe launcher for it, for anyone who would
# rather type `osr` from an ordinary cmd.exe or double-click.

param(
    [Parameter(Position = 0)]
    [string]$Command = "",

    [Parameter(Position = 1, ValueFromRemainingArguments = $true)]
    [string[]]$Rest = @()
)

$ScriptDir = $PSScriptRoot
Set-Location $ScriptDir

$Osr = Join-Path $ScriptDir "build\osr.exe"

function ErrMsg([string]$msg) { Write-Host $msg -ForegroundColor Red }

function Show-Usage {
    Write-Host "Usage:"
    Write-Host "  osr.ps1 install <rice> [-Theme <name>]"
    Write-Host "  osr.ps1 switch  <rice> [-Theme <name>]  (same engine as install)"
    Write-Host "  osr.ps1 theme   [<name>]         re-theme (a name) or print current (none)"
    Write-Host "  osr.ps1 wallpaper [<path>|-List|-Next]"
    Write-Host "  osr.ps1 module <name>... [-Theme <name>]"
    Write-Host "  osr.ps1 list                     list available rices"
    Write-Host "  osr.ps1 themes                   list available themes"
    Write-Host "  osr.ps1 modules                  list available modules"
    Write-Host "  osr.ps1 test                     build + run the unit tests"
    Write-Host ""
    Write-Host "Anything else is passed straight to build\osr.exe, which is the same"
    Write-Host "core the POSIX ./osr drives -- run it with no arguments for the full"
    Write-Host "command list."
    Write-Host ""
    Write-Host "Windows OS passes (they change the system, not your apps -- ask for"
    Write-Host "them by name, they are never part of a rice):"
    Write-Host "  osr.ps1 module win-tweaks    debloat services, Explorer/taskbar/snap, sudo"
    Write-Host "  osr.ps1 module win-update    ask Windows Update to run now"
    Write-Host "  osr.ps1 module win-debloat   Raphire's Win11Debloat (third-party)"
    Write-Host "  osr.ps1 module win-winutil   Chris Titus WinUtil (third-party, interactive)"
}

# Ensure-Nob -- compile build\nob.exe from nob.c if it is not there yet,
# creating build\ first: that is the one directory the bootstrap cannot ask nob
# to make for it.
function Ensure-Nob {
    $buildDir = Join-Path $ScriptDir "build"
    if (Test-Path (Join-Path $buildDir "nob.exe")) { return $true }
    $gcc = Get-Command gcc -ErrorAction SilentlyContinue
    if (-not $gcc) {
        ErrMsg "build\nob.exe not found and no gcc on PATH -- install a mingw-w64 gcc (e.g. 'scoop install gcc') and try again"
        return $false
    }
    Write-Host "build\nob.exe not found -- bootstrapping it with gcc..." -ForegroundColor Cyan
    if (-not (Test-Path $buildDir)) { New-Item -ItemType Directory -Path $buildDir | Out-Null }
    & gcc -o build\nob.exe nob.c
    return $LASTEXITCODE -eq 0
}

# Ensure-Osr -- build\osr.exe, up to date. nob does nothing when it already is.
function Ensure-Osr {
    if (-not (Ensure-Nob)) { return $false }
    & .\build\nob.exe
    return $LASTEXITCODE -eq 0
}

# Convert-Flags -- the PowerShell spelling of the options into the core's own.
# -Theme is the only one worth translating; everything else passes through, so
# a flag the core grows needs no change here.
function Convert-Flags([string[]]$args) {
    $out = @()
    for ($i = 0; $i -lt $args.Count; $i++) {
        if ($args[$i] -eq "-Theme" -and $i + 1 -lt $args.Count) {
            $out += @("--theme", $args[$i + 1]); $i++
        } elseif ($args[$i] -eq "-List") { $out += "--list" }
        elseif ($args[$i] -eq "-Next")  { $out += "--next" }
        else { $out += $args[$i] }
    }
    return $out
}

switch ($Command) {
    { $_ -in @("install", "switch") } {
        if (-not (Ensure-Osr)) { exit 1 }
        if ($Rest.Count -eq 0 -or $Rest[0].StartsWith("-")) {
            ErrMsg "osr.ps1 $Command <rice> [-Theme <name>]"
            exit 1
        }
        & $Osr install run @(Convert-Flags $Rest)
        exit $LASTEXITCODE
    }
    "theme" {
        if (-not (Ensure-Osr)) { exit 1 }
        if ($Rest.Count -eq 0) {
            # The current theme, read through the core rather than off disk:
            # where the state file lives is the core's business.
            & $Osr state get theme
            Write-Host ""
            exit 0
        }
        & $Osr install run --theme-only --theme $Rest[0]
        exit $LASTEXITCODE
    }
    "wallpaper" {
        if (-not (Ensure-Osr)) { exit 1 }
        & $Osr wallpaper @(Convert-Flags $Rest)
        exit $LASTEXITCODE
    }
    "module" {
        if (-not (Ensure-Osr)) { exit 1 }
        if ($Rest.Count -eq 0) {
            ErrMsg "osr.ps1 module <name>... [-Theme <name>]"
            exit 1
        }
        & $Osr install run --module @(Convert-Flags $Rest)
        exit $LASTEXITCODE
    }
    "list" {
        if (-not (Ensure-Osr)) { exit 1 }
        & $Osr install list-rices
        exit $LASTEXITCODE
    }
    "themes" {
        if (-not (Ensure-Osr)) { exit 1 }
        & $Osr theme list
        exit $LASTEXITCODE
    }
    "modules" {
        if (-not (Ensure-Osr)) { exit 1 }
        & $Osr install list-modules
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
        # Every core command by its own name: detect, pkg, build, config,
        # state, service, fonts, migrate, apply, reload, preflight, git.
        if (-not (Ensure-Osr)) { exit 1 }
        & $Osr $Command @Rest
        exit $LASTEXITCODE
    }
}
