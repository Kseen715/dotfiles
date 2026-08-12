# os-rice/windows-rice/src/theme.ps1 — theme layer resolution.
#
# Mirrors ../../lib/theme.sh's install_theme_layer / osr_theme_source: a
# theme's own file wins when it ships one, the caller (a modules/*.ps1) falls
# back to its dotfiles default otherwise. No template-substitution engine
# ported (see themes/osr-rice/theme.list) -- with one theme so far, a literal
# file per app is the whole mechanism, same as Linux's own escape hatch.

$script:ThemesRoot = Join-Path $PSScriptRoot "..\themes"

# Get-ThemeConfig -App wezterm -FileName wezterm-theme.toml -Theme osr-rice
# Returns the theme's literal file path for that app if it ships one, else
# $null -- the caller then falls back to its own dotfiles-owned default.
function Get-ThemeConfig {
    param(
        [Parameter(Mandatory)][string]$App,
        [Parameter(Mandatory)][string]$FileName,
        [string]$Theme = "osr-rice"
    )
    $path = Join-Path $script:ThemesRoot "$Theme\config\$App\$FileName"
    if (Test-Path -Path $path -PathType Leaf) { return $path }
    return $null
}
