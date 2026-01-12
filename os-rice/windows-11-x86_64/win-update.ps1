. $PSScriptRoot/src/common.ps1

Invoke-ElevatedScript

# Update Windows
InvokeMicroscript "update-windows.ps1" $PSScriptRoot