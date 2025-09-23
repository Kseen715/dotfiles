$regPath = "HKCU:Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced"
$regName = "SnapAssist"
$regValue = $args[0]

if (-not $ScriptRoot) {
    $ScriptRoot = $PSScriptRoot
}
. $ScriptRoot/src/common.ps1
if ($args.Length -eq 0) {
    $regValue = 1
}
UpdateRegistryValue -Path $regPath -Name $regName -Value $regValue
