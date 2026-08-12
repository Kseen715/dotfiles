# os-rice/windows-rice/rice.ps1 — install (or save) a rice: read
# rices/<rice>/rice.list, run each module's package + config + theme layer
# (modules/<name>.ps1). Windows' own tree (../DESIGN.md keeps it out of the
# POSIX os-rice/lib abstraction on purpose) but the same shape as the Linux
# side: lib (src/) + modules + rices + themes, one runner that knows the
# whole rice. Reruns are idempotent (every step is a "skip if already there"
# check); config overwrites default to yes (-Ask to confirm each one).
#
# No elevation required: scoop/winget user installs and config copies need no
# admin ticket. If choco ends up being the only manager available and it isn't
# installed yet, its own installer will prompt for elevation itself.

param(
    [string]$Rice = "default",
    [string]$Theme = "osr-rice",
    [string]$Module = "",
    [switch]$Save = $false,
    [switch]$Ask = $false
)

. $PSScriptRoot/src/common.ps1
. $PSScriptRoot/src/pkg.ps1
. $PSScriptRoot/src/fonts.ps1
. $PSScriptRoot/src/config.ps1
. $PSScriptRoot/src/theme.ps1

# Used implicitly by every modules/*.ps1 below (dot-sourced into this same
# scope, so they see it without it being passed) -- same "detect/resolve once,
# read everywhere" convention as $OSR_DOTFILES on the Linux side.
$REPO_ROOT = Resolve-Path (Join-Path $PSScriptRoot "..\..")

Get-ChildItem (Join-Path $PSScriptRoot "modules") -Filter "*.ps1" | ForEach-Object { . $_.FullName }

# module name -> the Verb-Noun suffix its Install-/Save- functions use
# (modules/oh-my-posh.ps1 defines Install-OhMyPosh, not Install-Oh-My-Posh).
$ModuleFns = @{
    pwsh         = 'Pwsh'
    wezterm      = 'Wezterm'
    'oh-my-posh' = 'OhMyPosh'
    fastfetch    = 'Fastfetch'
}

if ($Module) {
    $modules = @($Module)
} else {
    $riceListPath = Join-Path $PSScriptRoot "rices\$Rice\rice.list"
    if (-not (Test-Path $riceListPath)) {
        EchoError "No such rice '$Rice' ($riceListPath not found)"
        exit 1
    }
    $modules = Get-Content $riceListPath |
        ForEach-Object { $_.Trim() } |
        Where-Object { $_ -and -not $_.StartsWith('#') }
}

$verb = if ($Save) { 'Saving' } else { 'Installing' }
EchoInfo "=== $verb rice '$Rice' (theme: $Theme) ==="

foreach ($m in $modules) {
    if (-not $ModuleFns.ContainsKey($m)) {
        EchoWarning "No module for '$m', skipping"
        continue
    }
    $fn = "$(if ($Save) { 'Save' } else { 'Install' })-$($ModuleFns[$m])"
    & $fn -Ask:$Ask -Theme $Theme
}

EchoInfo "Done.$(if (-not $Save) { ' Open a new WezTerm / pwsh window to see the rice.' })"
