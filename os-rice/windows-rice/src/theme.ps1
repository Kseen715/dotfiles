# os-rice/windows-rice/src/theme.ps1 — theme layer resolution + template
# rendering. A PowerShell port of ../../lib/theme.sh's _osr_theme_sed /
# render_theme_template and ../../lib/config.sh's osr_theme_source /
# install_theme_layer, NOT a Windows-only reimplementation: it reads the same
# theme.list format and the same *.tmpl files the Linux rices use, so a
# Windows install can render, say, ../../themes/nord/theme.list against
# ../../../fastfetch/config.jsonc.tmpl and get a real nord fastfetch, never a
# hand-copied approximation that drifts from what Linux actually ships.
#
# Resolution order for -App/-FileName/-Theme (Get-ThemeSource):
#   1. themes/<Theme>/config/<App>/<FileName>   Windows-local literal file --
#      the same escape hatch Linux themes have for a look that isn't a
#      palette substitution (see install_theme_layer's own comment).
#   2. themes/<Theme>/theme.list                 a Windows-native theme
#   3. ../../themes/<Theme>/theme.list            -- OR -- a real Linux one
#      -- whichever exists, render ../../../<App>/<FileName>.tmpl against it.
#   4. neither exists -> $null, caller's cue to fall back to its own
#      dotfiles-owned default (same contract as Linux's install_theme_layer).

$script:ThemesRoot = Join-Path $PSScriptRoot "..\themes"

# Get-ThemeConfig -App wezterm -FileName wezterm-theme.toml -Theme osr-rice
# Literal-file lookup only (step 1 above) -- kept as its own function because
# a couple of callers (oh-my-posh, which has no .tmpl at all) only ever want
# this half of the resolution chain.
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

# Resolve-ThemeListPath osr-rice -> windows-rice/themes/osr-rice/theme.list
# Resolve-ThemeListPath nord     -> ../../themes/nord/theme.list (Linux)
function Resolve-ThemeListPath {
    param([Parameter(Mandatory)][string]$Theme)

    $winPath = Join-Path $script:ThemesRoot "$Theme\theme.list"
    if (Test-Path -Path $winPath -PathType Leaf) { return $winPath }

    $linuxPath = Join-Path $REPO_ROOT "os-rice\themes\$Theme\theme.list"
    if (Test-Path -Path $linuxPath -PathType Leaf) { return $linuxPath }

    return $null
}

# Mirrors lib/theme.sh's _osr_theme_lines: strip full-line and inline
# comments, trim, drop blanks. A palette value IS a hash (#rrggbb), so only a
# `#` with whitespace on both sides -- or one that starts the line -- counts
# as a comment; `#rrggbb` never has a space after the hash, so the two can
# never collide.
function Get-ThemeListLines {
    param([Parameter(Mandatory)][string]$Path)
    # Same explicit-UTF-8 reasoning as Expand-ThemeTemplate: comments get
    # stripped below regardless, but a value (a theme's `description:`, say)
    # could carry non-ASCII too, and Get-Content's codepage guess must not be
    # the thing deciding whether that survives.
    $lines = [System.IO.File]::ReadAllLines($Path, [System.Text.Encoding]::UTF8)
    $lines | ForEach-Object {
        $line = $_ -replace '^\s*#.*$', ''
        $line = $line -replace '\s#\s.*$', ''
        $line = $line -replace '\s#$', ''
        $line.Trim()
    } | Where-Object { $_ }
}

# Get-ThemePalette nord -> @{ Name=nord; Colors=@{background='#2e3440'; ...}; Meta=@{display='Nord'; ...} }
function Get-ThemePalette {
    param([Parameter(Mandatory)][string]$Theme)

    $path = Resolve-ThemeListPath -Theme $Theme
    if (-not $path) { return $null }

    $colors = @{}
    $meta = @{}
    foreach ($line in (Get-ThemeListLines -Path $path)) {
        if ($line -match '^color:\s*([A-Za-z0-9_]+)\s+(.+)$') {
            $colors[$Matches[1]] = $Matches[2].Trim()
        } elseif ($line -match '^config:') {
            continue
        } elseif ($line -match '^([a-z][a-z0-9_]*):\s*(.*)$') {
            $meta[$Matches[1]] = $Matches[2].Trim()
        }
    }
    return @{ Name = $Theme; Colors = $colors; Meta = $meta }
}

function ConvertTo-DecimalRgb {
    param([Parameter(Mandatory)][string]$Hex)
    $h = $Hex.TrimStart('#')
    $r = [Convert]::ToInt32($h.Substring(0, 2), 16)
    $g = [Convert]::ToInt32($h.Substring(2, 2), 16)
    $b = [Convert]::ToInt32($h.Substring(4, 2), 16)
    return "$r,$g,$b"
}

# Expand-ThemeTemplate -TemplatePath fastfetch\config.jsonc.tmpl -Theme nord
# Every color role gets the same three extra spellings render_theme_template
# does: {{role}} #rrggbb, {{role_rgb}} rrggbb, {{role_dec}} r,g,b,
# {{role_sgr}} r;g;b -- only when the value IS a #rrggbb (background_opacity
# etc. are colors too but not hex, so they only ever get the bare {{role}}
# spelling, same branch Linux's sed takes). Meta fields (display, gtk_theme,
# ...) and {{THEME}} substitute once, literally. An unresolved {{...}} left
# over is a warning, not a failure -- the file still lands (matches
# render_theme_template's own contract).
function Expand-ThemeTemplate {
    param(
        [Parameter(Mandatory)][string]$TemplatePath,
        [Parameter(Mandatory)][string]$Theme
    )

    $palette = Get-ThemePalette -Theme $Theme
    if (-not $palette) {
        EchoError "No theme.list for '$Theme' (checked themes/$Theme/ and ../os-rice/themes/$Theme/)"
        return $null
    }

    # Explicit UTF-8 read: these .tmpl files carry real non-ASCII prose in
    # their comments (—, §, ...), and Get-Content -Raw with no -Encoding on
    # Windows PowerShell 5.1 decodes a BOM-less file through the system
    # codepage instead -- the exact bug class ../../DESIGN.md's ASCII-output
    # rule exists to dodge, this time on the READ side: it would silently
    # corrupt those bytes into the rendered file rather than fail loudly.
    $text = [System.IO.File]::ReadAllText($TemplatePath, [System.Text.Encoding]::UTF8)
    $text = $text.Replace('{{THEME}}', $palette.Name)

    foreach ($role in $palette.Colors.Keys) {
        $value = $palette.Colors[$role]
        if ($value -match '^#[0-9a-fA-F]{6}$') {
            $dec = ConvertTo-DecimalRgb $value
            $text = $text.Replace("{{${role}}}", $value)
            $text = $text.Replace("{{${role}_rgb}}", $value.Substring(1))
            $text = $text.Replace("{{${role}_dec}}", $dec)
            $text = $text.Replace("{{${role}_sgr}}", ($dec -replace ',', ';'))
        } else {
            $text = $text.Replace("{{${role}}}", $value)
        }
    }
    foreach ($key in $palette.Meta.Keys) {
        $text = $text.Replace("{{${key}}}", $palette.Meta[$key])
    }

    $leftover = [regex]::Matches($text, '\{\{([A-Za-z0-9_]+)\}\}') |
        ForEach-Object { $_.Groups[1].Value } |
        Where-Object { $_ -ne 'WALLPAPER_PATH' } |
        Select-Object -Unique
    if ($leftover) {
        EchoWarning "theme '$Theme' defines no $($leftover -join ' ') -- left unsubstituted in $(Split-Path $TemplatePath -Leaf)"
    }

    return $text
}

# Get-ThemeSource -App wezterm -FileName wezterm-theme.toml -Theme nord
# The full chain (steps 1-4 above): a path ready to install, or $null. A
# rendered template is written to a temp file the caller must remove --
# Install-ThemeLayer below does that for you and is what modules normally use.
function Get-ThemeSource {
    param(
        [Parameter(Mandatory)][string]$App,
        [Parameter(Mandatory)][string]$FileName,
        [string]$Theme = "osr-rice"
    )

    $literal = Get-ThemeConfig -App $App -FileName $FileName -Theme $Theme
    if ($literal) { return $literal }

    $tmplPath = Join-Path $REPO_ROOT "$App\$FileName.tmpl"
    if (-not (Test-Path -Path $tmplPath -PathType Leaf)) { return $null }

    $rendered = Expand-ThemeTemplate -TemplatePath $tmplPath -Theme $Theme
    if ($null -eq $rendered) { return $null }

    $tmpPath = Join-Path $env:TEMP "osr-theme-$App-$([guid]::NewGuid().ToString('N').Substring(0, 8))-$FileName"
    [System.IO.File]::WriteAllText($tmpPath, $rendered, (New-Object System.Text.UTF8Encoding($false)))
    return $tmpPath
}

# Install-ThemeLayer -App wezterm -FileName wezterm-theme.toml -Destination "~\.config\wezterm\colors\osr-rice.toml" -Theme nord
# The whole per-app theme rule in one call, mirrors install_theme_layer:
# resolve + copy + clean up the temp file if one was made. Returns $false
# (nothing installed) when the theme has neither a literal file nor a
# renderable template -- the module's cue to fall back to its own unthemed
# dotfiles default.
function Install-ThemeLayer {
    param(
        [Parameter(Mandatory)][string]$App,
        [Parameter(Mandatory)][string]$FileName,
        [Parameter(Mandatory)][string]$Destination,
        [string]$Theme = "osr-rice",
        [switch]$Ask
    )
    $src = Get-ThemeSource -App $App -FileName $FileName -Theme $Theme
    if (-not $src) { return $false }
    Copy-ConfigEntry -Source $src -Destination $Destination -Ask:$Ask
    if ($src -like "$env:TEMP\osr-theme-*") { Remove-Item -Force $src -ErrorAction SilentlyContinue }
    return $true
}
