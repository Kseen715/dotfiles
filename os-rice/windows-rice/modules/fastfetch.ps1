# os-rice/windows-rice/modules/fastfetch.ps1 — fastfetch + theme-owned config.
# Mirrors ../../modules/fastfetch.sh: fastfetch reads exactly one config.jsonc,
# so the theme owns the whole installed file (it's nothing but presentation).
# Rendered dynamically from ../../../fastfetch/config.jsonc.tmpl -- the SAME
# template the Linux rices use -- against whichever theme.list -Theme
# resolves to (src/theme.ps1); nothing here is a hand-copied approximation of
# the Linux config, so it can never drift from it.
#
# ~\.config\fastfetch\config.jsonc is fastfetch's own first config search path
# on Windows too (confirmed via `fastfetch --list-config-paths`), so nothing
# needs to pass -c/--config at call time -- it's auto-discovered exactly like
# on Linux.

function Install-Fastfetch {
    param([switch]$Ask, [string]$Theme = "osr-rice")

    $null = Install-RicePackage -Name "fastfetch"

    if (-not (Install-ThemeLayer -App "fastfetch" -FileName "config.jsonc" `
            -Destination "~\.config\fastfetch\config.jsonc" -Theme $Theme -Ask:$Ask)) {
        EchoWarning "No fastfetch config.jsonc.tmpl or theme '$Theme'; leaving fastfetch's own default"
    }
}

function Save-Fastfetch {
    param([switch]$Ask, [string]$Theme = "osr-rice")
    EchoInfo "fastfetch's config.jsonc is rendered from ../../../fastfetch/config.jsonc.tmpl + a theme's palette -- edit the template or the theme's theme.list, not the installed file"
}
