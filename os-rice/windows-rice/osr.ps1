# os-rice/windows-rice/osr.ps1 — CLI front end, mirrors ../osr (install /
# switch / theme / wallpaper / list) scaled to what Windows actually has: one
# rice, one theme, four modules so far. Thin dispatcher over rice.ps1, which
# does the actual work; this just gives it the same verbs.
#
#   osr.ps1 install [-Rice default] [-Theme osr-rice] [-Ask]
#   osr.ps1 save    [-Rice default] [-Theme osr-rice] [-Ask]
#   osr.ps1 module <name> [-Theme osr-rice] [-Ask] [-Save]
#   osr.ps1 list

param(
    [Parameter(Position = 0)]
    [ValidateSet('install', 'save', 'module', 'list')]
    [string]$Command = 'install',

    [Parameter(Position = 1)]
    [string]$Name = "",

    [string]$Rice = "default",
    [string]$Theme = "osr-rice",
    [switch]$Ask = $false
)

. $PSScriptRoot/src/common.ps1

switch ($Command) {
    'install' {
        & $PSScriptRoot/rice.ps1 -Rice $Rice -Theme $Theme -Ask:$Ask
    }
    'save' {
        & $PSScriptRoot/rice.ps1 -Rice $Rice -Theme $Theme -Ask:$Ask -Save
    }
    'module' {
        if (-not $Name) {
            EchoError "osr.ps1 module <name> -- e.g. 'osr.ps1 module wezterm'"
            exit 1
        }
        & $PSScriptRoot/rice.ps1 -Module $Name -Theme $Theme -Ask:$Ask
    }
    'list' {
        EchoInfo "Rices:"
        Get-ChildItem (Join-Path $PSScriptRoot "rices") -Directory |
            ForEach-Object { Write-Host "  $($_.Name)" }
        EchoInfo "Themes:"
        Get-ChildItem (Join-Path $PSScriptRoot "themes") -Directory |
            ForEach-Object { Write-Host "  $($_.Name)" }
        EchoInfo "Modules:"
        Get-ChildItem (Join-Path $PSScriptRoot "modules") -Filter "*.ps1" |
            ForEach-Object { Write-Host "  $($_.BaseName)" }
    }
}
