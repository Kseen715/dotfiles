# os-rice/windows-rice/modules/oh-my-posh.ps1 — oh-my-posh + Nerd Font + theme.
# Mirrors ../../modules/starship.sh's shape, but oh-my-posh has no separate
# base-config layer: the .omp.json IS the whole prompt definition (structure
# and colors together), so there's nothing dotfiles-owned to install alongside
# it -- only a theme-owned file, always resolved through Get-ThemeConfig. Only
# 'osr-rice' ships one today, so Install-OhMyPosh falls back to it with a
# warning for any -Theme (like the default 'xin') that has no prompt of its
# own -- the color palette (wezterm/fastfetch) still switches, only the
# oh-my-posh prompt itself stays put until a themes/<name>/config/oh-my-posh/
# file exists.

function Resolve-PoshThemesPath {
    # POSH_THEMES_PATH is set by the installer but a session that hasn't
    # refreshed its env (Update-SessionEnvironment) or never had it set at all
    # won't see it. scoop's `oh-my-posh` on PATH is a shim in ~\scoop\shims,
    # not the real app dir, so `scoop prefix` (not Get-Command) resolves to
    # ~\scoop\apps\oh-my-posh\current\themes.
    $themesPath = $env:POSH_THEMES_PATH
    if (-not $themesPath -and (Test-Command scoop)) {
        $prefix = scoop prefix oh-my-posh 2>$null
        if ($prefix) { $themesPath = Join-Path $prefix "themes" }
    }
    if (-not $themesPath) {
        $bin = Get-Command oh-my-posh -ErrorAction SilentlyContinue
        if ($bin) { $themesPath = Join-Path (Split-Path $bin.Source -Parent) "themes" }
    }
    if ($themesPath -and (Test-Path $themesPath)) { return $themesPath }
    return $null
}

function Install-OhMyPosh {
    param([switch]$Ask, [string]$Theme = "osr-rice")

    $null = Install-RicePackage -Name "oh-my-posh"
    Install-NerdFont -Name "JetBrainsMono"

    $themesPath = Resolve-PoshThemesPath
    if (-not $themesPath) {
        EchoError "Could not resolve oh-my-posh's themes directory; is oh-my-posh installed?"
        return
    }
    $themeFile = Get-ThemeConfig -App "oh-my-posh" -FileName "M365Princess++.omp.json" -Theme $Theme
    if (-not $themeFile -and $Theme -ne "osr-rice") {
        EchoWarning "Theme '$Theme' ships no oh-my-posh config (themes/$Theme/config/oh-my-posh/); using 'osr-rice' -- the only prompt defined so far"
        $themeFile = Get-ThemeConfig -App "oh-my-posh" -FileName "M365Princess++.omp.json" -Theme "osr-rice"
    }
    if (-not $themeFile) {
        EchoError "Theme '$Theme' ships no oh-my-posh config (themes/$Theme/config/oh-my-posh/)"
        return
    }

    Install-RiceConfig -Ask:$Ask `
        -Files @("$themesPath\M365Princess++.omp.json") `
        -LocalFiles @($themeFile)
}

function Save-OhMyPosh {
    param([switch]$Ask, [string]$Theme = "osr-rice")

    $themesPath = Resolve-PoshThemesPath
    if (-not $themesPath) {
        EchoError "Could not resolve oh-my-posh's themes directory; is oh-my-posh installed?"
        return
    }
    $themeFile = Get-ThemeConfig -App "oh-my-posh" -FileName "M365Princess++.omp.json" -Theme $Theme
    if (-not $themeFile) {
        EchoError "Theme '$Theme' ships no oh-my-posh config (themes/$Theme/config/oh-my-posh/)"
        return
    }

    Save-RiceConfig -Ask:$Ask `
        -Files @("$themesPath\M365Princess++.omp.json") `
        -LocalFiles @($themeFile)
}
