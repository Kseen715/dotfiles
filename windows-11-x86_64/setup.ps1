function InvokeEcho {
    param([string]$Command)
    Write-Host "[PWSH]`t" -ForegroundColor Cyan -NoNewline
    Write-Host $Command 
    Invoke-Expression $Command
}

# Disable the window shake gesture
InvokeEcho "Start-Process powershell -ArgumentList '-NoProfile -ExecutionPolicy Bypass -File `"$PSScriptRoot\microscripts\window-shake-gesture-disable.ps1`"' -Verb RunAs -Wait -PassThru | Out-Null"
