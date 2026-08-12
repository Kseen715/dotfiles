# os-rice/windows-rice/modules/wezterm.ps1 — WezTerm + Nerd Font + config +
# theme. Mirrors ../../modules/wezterm.sh: base config is dotfiles-owned
# (../../../wezterm/.wezterm.lua, overwritten on update), the palette is
# rendered dynamically from ../../../wezterm/wezterm-theme.toml.tmpl -- the
# SAME template the Linux rices use -- against whichever theme.list -Theme
# resolves to (src/theme.ps1), and installed to
# ~/.config/wezterm/colors/osr-rice.toml, which .wezterm.lua selects by name.
#
# Falls back to the literal ../../../wezterm/wezterm-theme.toml (Linux's own
# dotfiles-level default, read by its modules/wezterm.sh too) only if no
# theme.list resolves at all -- should not happen for the shipped "osr-rice"
# theme, but keeps this module from leaving WezTerm unthemed if it does.

function Install-Wezterm {
    param([switch]$Ask, [string]$Theme = "osr-rice")

    $null = Install-RicePackage -Name "wezterm"
    Install-NerdFont -Name "JetBrainsMono"

    $dotfiles = Join-Path $REPO_ROOT "wezterm"
    Copy-ConfigEntry -Source "$dotfiles\.wezterm.lua" -Destination "~\.wezterm.lua" -Ask:$Ask

    $dest = "~\.config\wezterm\colors\osr-rice.toml"
    if (-not (Install-ThemeLayer -App "wezterm" -FileName "wezterm-theme.toml" -Destination $dest -Theme $Theme -Ask:$Ask)) {
        Copy-ConfigEntry -Source "$dotfiles\wezterm-theme.toml" -Destination $dest -Ask:$Ask
    }
}

function Save-Wezterm {
    param([switch]$Ask, [string]$Theme = "osr-rice")

    $dotfiles = Join-Path $REPO_ROOT "wezterm"
    Copy-ConfigEntry -Source "~\.wezterm.lua" -Destination "$dotfiles\.wezterm.lua" -Ask:$Ask
    EchoInfo "wezterm-theme.toml is rendered from ../../../wezterm/wezterm-theme.toml.tmpl + a theme's palette -- edit the template or the theme's theme.list, not the installed file"
}
