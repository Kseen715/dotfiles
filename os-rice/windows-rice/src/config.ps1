# os-rice/windows-rice/src/config.ps1 — shared config-copy engine.
#
# Used by every modules/*.ps1's Install-*/Save-* pair -- each app used to
# carry its own ~90-line copy-paste boilerplate for "copy these files/dirs,
# prompt before overwrite" (three near-identical top-level install.ps1/save.ps1
# scripts) -- exactly the DRY problem ../../DESIGN.md calls out on the Linux
# side ("byte-identical except one line"). One copy here; each module calls it
# with just its own file list.
#
# Default is to overwrite without asking -- this is a dotfiles rice, the repo
# is the source of truth, and a silent no-op default (requiring an opt-in
# `-y`) meant a plain `rice.ps1` run looked like it worked but left every
# already-existing config untouched. Pass -Ask to get the old interactive
# confirm-before-overwrite behavior back.

function Copy-ConfigEntry {
    param(
        [Parameter(Mandatory)][string]$Source,
        [Parameter(Mandatory)][string]$Destination,
        [switch]$IsDirectory,
        [switch]$Ask
    )

    $pathType = if ($IsDirectory) { 'Container' } else { 'Leaf' }
    $kind = if ($IsDirectory) { 'directory' } else { 'file' }

    if (-not (Test-Path -Path $Source -PathType $pathType)) {
        EchoError "Source $kind $Source doesn't exist"
        return
    }

    $destExists = Test-Path -Path $Destination -PathType $pathType
    if ($destExists -and $Ask) {
        EchoWarning "$Destination exists. Overwrite? (y/N) "
        $answer = Read-Host
        if ($answer -ne 'y') { return }
    }

    if ($IsDirectory) {
        if (-not $destExists) { New-Item -Path $Destination -ItemType Directory -Force | Out-Null }
        Copy-Item -Path "$Source\*" -Destination $Destination -Recurse -Force
    } else {
        $parent = Split-Path -Path $Destination -Parent
        if ($parent -and -not (Test-Path $parent)) { New-Item -Path $parent -ItemType Directory -Force | Out-Null }
        Copy-Item -Path $Source -Destination $Destination -Force
    }
    EchoInfo "Installed $Source -> $Destination"
}

# Install-RiceConfig -Ask:$ask `
#     -Files @("~\.wezterm.lua", "~\.config\wezterm\colors\osr-rice.toml") `
#     -LocalFiles @(".\.wezterm.lua", ".\wezterm-theme.toml")
#
# Files[i]/LocalFiles[i] pair up positionally, same for Directories/LocalDirectories.
# Overwrites existing destinations by default; -Ask confirms each one first.
function Install-RiceConfig {
    param(
        [string[]]$Files = @(),
        [string[]]$LocalFiles = @(),
        [string[]]$Directories = @(),
        [string[]]$LocalDirectories = @(),
        [switch]$Ask
    )

    if ($Directories.Count -ne $LocalDirectories.Count) {
        EchoError "Directory arrays don't match in length. Please check your configuration."
    } else {
        for ($i = 0; $i -lt $Directories.Count; $i++) {
            Copy-ConfigEntry -Source $LocalDirectories[$i] -Destination $Directories[$i] -IsDirectory -Ask:$Ask
        }
    }

    if ($Files.Count -ne $LocalFiles.Count) {
        EchoError "File arrays don't match in length. Please check your configuration."
    } else {
        for ($i = 0; $i -lt $Files.Count; $i++) {
            Copy-ConfigEntry -Source $LocalFiles[$i] -Destination $Files[$i] -Ask:$Ask
        }
    }
}

# The inverse direction (save.ps1): copy the live installed config back into
# the repo. Same pairing, source and destination swapped.
function Save-RiceConfig {
    param(
        [string[]]$Files = @(),
        [string[]]$LocalFiles = @(),
        [string[]]$Directories = @(),
        [string[]]$LocalDirectories = @(),
        [switch]$Ask
    )
    Install-RiceConfig -Ask:$Ask `
        -Files $LocalFiles -LocalFiles $Files `
        -Directories $LocalDirectories -LocalDirectories $Directories
}
