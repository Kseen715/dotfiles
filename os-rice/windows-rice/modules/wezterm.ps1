# os-rice/windows-rice/modules/wezterm.ps1 — WezTerm + Nerd Font + config +
# theme. Mirrors ../../modules/wezterm.sh: base config is dotfiles-owned
# (../../../wezterm/.wezterm.lua, overwritten on update), the palette is
# theme-owned and installed to ~/.config/wezterm/colors/osr-rice.toml, which
# .wezterm.lua selects by that name.
#
# The palette itself: a theme's own config/wezterm/wezterm-theme.toml wins;
# with none, falls back to ../../../wezterm/wezterm-theme.toml -- the same
# file the LINUX build's modules/wezterm.sh falls back to (see
# themes/osr-rice/theme.list for why that file isn't duplicated in here).

function Install-Wezterm {
    param([switch]$Ask, [string]$Theme = "osr-rice")

    $null = Install-RicePackage -Name "wezterm"
    Install-NerdFont -Name "JetBrainsMono"

    $dotfiles = Join-Path $REPO_ROOT "wezterm"
    $themeFile = Get-ThemeConfig -App "wezterm" -FileName "wezterm-theme.toml" -Theme $Theme
    if (-not $themeFile) { $themeFile = Join-Path $dotfiles "wezterm-theme.toml" }

    Install-RiceConfig -Ask:$Ask `
        -Files @("~\.wezterm.lua", "~\.config\wezterm\colors\osr-rice.toml") `
        -LocalFiles @("$dotfiles\.wezterm.lua", $themeFile)
}

function Save-Wezterm {
    param([switch]$Ask, [string]$Theme = "osr-rice")

    $dotfiles = Join-Path $REPO_ROOT "wezterm"
    $themeFile = Get-ThemeConfig -App "wezterm" -FileName "wezterm-theme.toml" -Theme $Theme
    if (-not $themeFile) { $themeFile = Join-Path $dotfiles "wezterm-theme.toml" }

    Save-RiceConfig -Ask:$Ask `
        -Files @("~\.wezterm.lua", "~\.config\wezterm\colors\osr-rice.toml") `
        -LocalFiles @("$dotfiles\.wezterm.lua", $themeFile)
}
