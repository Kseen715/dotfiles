$regPath = "HKLM:Software\Microsoft\Windows\CurrentVersion\Sudo"
$regName = "Enabled"
$regValue = $args[0]

if (-not $ScriptRoot) {
    $ScriptRoot = $PSScriptRoot
}
. $ScriptRoot/src/common.ps1
if ($args.Length -eq 0) {
    $regValue = 3
}
UpdateRegistryValue -Path $regPath -Name $regName -Value $regValue
