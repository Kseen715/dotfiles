# os-rice/windows-rice/src/pkg.ps1 — package manager abstraction.
#
# Windows keeps its own package model, deliberately outside the POSIX
# os-rice/lib abstraction (see ../../DESIGN.md: "do NOT force it into this
# abstraction"). This mirrors the same idea from that design in Windows'
# native language instead: a logical name resolves through windows.map to a
# real id per manager, and install dispatches to whichever manager is
# actually installed, in scoop -> choco -> winget preference order (scoop
# needs no admin and is fastest for CLI tools; choco covers what scoop's
# buckets miss; winget is the built-in fallback everyone has).

$script:WindowsMapPath = Join-Path $PSScriptRoot "..\windows.map"

function Test-Command {
    param([Parameter(Mandatory)][string]$Name)
    return [bool](Get-Command $Name -ErrorAction SilentlyContinue)
}

function Install-Scoop {
    if (Test-Command scoop) { return }
    EchoInfo "Installing scoop (no admin required)..."
    Invoke-Expression (New-Object System.Net.WebClient).DownloadString('https://get.scoop.sh')
    Update-SessionEnvironment
}

function Get-AvailablePkgManagers {
    $mgrs = @()
    if (Test-Command scoop)  { $mgrs += 'scoop' }
    if (Test-Command choco)  { $mgrs += 'choco' }
    if (Test-Command winget) { $mgrs += 'winget' }
    return $mgrs
}

# Get-WindowsMapSpec wezterm  ->  @{ scoop = 'wezterm'; choco = 'wezterm'; winget = 'wez.wezterm' }
function Get-WindowsMapSpec {
    param([Parameter(Mandatory)][string]$Name)

    $spec = @{}
    foreach ($line in Get-Content $script:WindowsMapPath) {
        $trimmed = $line.Trim()
        if (-not $trimmed -or $trimmed.StartsWith('#')) { continue }
        if ($trimmed -notmatch '^(?<name>\S+)\s*=\s*(?<rhs>.+)$') { continue }
        if ($Matches.name -ne $Name) { continue }
        foreach ($tok in ($Matches.rhs -split '\s+')) {
            if ($tok -match '^(?<mgr>scoop|choco|winget):(?<id>.+)$') {
                $spec[$Matches.mgr] = $Matches.id
            }
        }
    }
    return $spec
}

# Install-RicePackage wezterm
#
# Idempotent: skipped if $TestCommand (defaults to $Name -- true for every
# entry windows.map has today) is already on PATH. Looks up windows.map for
# per-manager ids, then tries scoop -> choco -> winget among the managers
# actually installed, falling back to installing scoop itself (the one
# no-admin option) if none are present at all.
function Install-RicePackage {
    param(
        [Parameter(Mandatory)][string]$Name,
        [string]$TestCommand = $Name
    )

    if (Test-Command $TestCommand) {
        EchoInfo "$Name already installed (found '$TestCommand'), skipping"
        return $true
    }

    $spec = Get-WindowsMapSpec -Name $Name
    if ($spec.Count -eq 0) {
        EchoError "No windows.map entry for '$Name'"
        return $false
    }

    $available = Get-AvailablePkgManagers
    if ($available.Count -eq 0) {
        Install-Scoop
        $available = Get-AvailablePkgManagers
    }

    foreach ($mgr in @('scoop', 'choco', 'winget')) {
        if ($available -notcontains $mgr) { continue }
        if (-not $spec.ContainsKey($mgr)) { continue }
        $id = $spec[$mgr]
        EchoInfo "Installing $Name via $mgr ($id)..."
        switch ($mgr) {
            'scoop'  { InvokeEcho "scoop install $id" }
            'choco'  { InvokeEcho "choco install $id -y" }
            'winget' { InvokeEcho "winget install --id $id -e --accept-source-agreements --accept-package-agreements" }
        }
        Update-SessionEnvironment
        if (Test-Command $TestCommand) { return $true }
        EchoWarning "$Name still not found after $mgr install, trying next manager..."
    }

    EchoError "Could not install $Name with any available package manager ($($available -join ', '))"
    return $false
}
