# os-rice/windows-rice/modules/fastfetch.ps1 — fastfetch package only.
# ff-startup.jsonc is config, but it's loaded by the pwsh profile
# ($ffConfigPath in PowerShell7-profile/Microsoft.PowerShell_profile.ps1) and
# lives beside it, not here -- modules/pwsh.ps1 already installs it. Nothing
# left for this module to own but the package.

function Install-Fastfetch {
    param([switch]$Ask, [string]$Theme = "osr-rice")
    $null = Install-RicePackage -Name "fastfetch"
}

function Save-Fastfetch {
    param([switch]$Ask, [string]$Theme = "osr-rice")
    EchoInfo "fastfetch has no config layer of its own (see modules/pwsh.ps1), nothing to save"
}
