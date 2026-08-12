# os-rice/windows-rice/modules/pwsh.ps1 — PowerShell 7 + profile config.
# Mirrors ../../modules/zsh.sh's shape: package, then dotfiles-owned config
# (profile.ps1 is the loader, no theme layer -- oh-my-posh owns the prompt
# theme, see modules/oh-my-posh.ps1).
#
# Profile path is asked FROM THE INSTALLED PWSH ITSELF
# ($PROFILE.CurrentUserCurrentHost), never assembled from $HOME\Documents --
# a redirected/OneDrive-moved Documents folder (common) makes those two
# disagree, and a profile written to the wrong one silently never loads. This
# is what "PowerShell profile is not applied" turned out to be.

function Resolve-PwshProfilePath {
    if (-not (Test-Command pwsh)) { return $null }
    $path = & pwsh -NoLogo -NoProfile -Command '$PROFILE.CurrentUserCurrentHost'
    if ([string]::IsNullOrWhiteSpace($path)) { return $null }
    return $path.Trim()
}

function Install-Pwsh {
    param([switch]$Ask, [string]$Theme = "osr-rice")

    $null = Install-RicePackage -Name "pwsh"

    $profilePath = Resolve-PwshProfilePath
    if (-not $profilePath) {
        EchoError "Could not resolve pwsh's own profile path; is pwsh installed?"
        return
    }

    $dotfiles = Join-Path $REPO_ROOT "PowerShell7-profile"
    Install-RiceConfig -Ask:$Ask `
        -Files @($profilePath, "$(Split-Path -Path $profilePath -Parent)\ff-startup.jsonc") `
        -LocalFiles @("$dotfiles\Microsoft.PowerShell_profile.ps1", "$dotfiles\ff-startup.jsonc")
}

function Save-Pwsh {
    param([switch]$Ask, [string]$Theme = "osr-rice")

    $profilePath = Resolve-PwshProfilePath
    if (-not $profilePath) {
        EchoError "Could not resolve pwsh's own profile path; is pwsh installed?"
        return
    }

    $dotfiles = Join-Path $REPO_ROOT "PowerShell7-profile"
    Save-RiceConfig -Ask:$Ask `
        -Files @($profilePath, "$(Split-Path -Path $profilePath -Parent)\ff-startup.jsonc") `
        -LocalFiles @("$dotfiles\Microsoft.PowerShell_profile.ps1", "$dotfiles\ff-startup.jsonc")
}
