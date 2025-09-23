function InvokeEcho {
    param([string]$Command)
    Write-Host "[PWSH ]`t" -ForegroundColor Cyan -NoNewline
    Write-Host $Command 
    Invoke-Expression $Command
}

function EchoInfo {
    param([string]$Message)
    Write-Host "[INFO ]`t" -ForegroundColor White -NoNewline
    Write-Host $Message
}

function EchoWarning {
    param([string]$Message)
    Write-Host "[WARN ]`t" -ForegroundColor Yellow -NoNewline
    Write-Host $Message
}

function EchoError {
    param([string]$Message)
    Write-Host "[ERROR]`t" -ForegroundColor Red -NoNewline
    Write-Host $Message
}

function InvokeMicroscript {
    param(
        [string]$MicroscriptName,
        [string]$ScriptRoot = $PSScriptRoot
    )
    $command = "& `"$ScriptRoot\microscripts\$MicroscriptName`""
    InvokeEcho $command
}

function UpdateRegistryValue {
    param (
        [string]$Path,
        [string]$Name,
        [int]$Value
    )
    try {
        EchoInfo "REGEDIT: $regPath\$regName to $regValue"
        Set-ItemProperty -Path $regPath -Name $regName -Value $regValue
    } catch {
        EchoError "Failed to update registry: $regPath\$regName to $regValue, $_"
    }
}

function InvokeRegMicroscript {
    param(
        [string]$MicroscriptName,
        [string]$RegistryValue,
        [string]$ScriptRoot = $PSScriptRoot
    )
    $command = "& `"$ScriptRoot\microscripts\$MicroscriptName`" $RegistryValue"
    InvokeEcho $command
}

function Test-IsElevated {
    $currentPrincipal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    return $currentPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Invoke-ElevatedScript {
    param(
        [string]$ScriptPath = $MyInvocation.PSCommandPath,
        [string[]]$Arguments = @(),
        [int]$WaitSeconds = 30
    )
    
    if (Test-IsElevated) {
        EchoInfo "Running with elevated privileges"
        return $true
    }
    
    EchoWarning "Script requires elevation. Restarting with administrator privileges..."
    
    try {
        # Create a wrapper script that includes the wait
        $wrapperScript = @"
try {
    & '$ScriptPath' $($Arguments -join ' ')
    `$exitCode = `$LASTEXITCODE
    Write-Host ""
    Write-Host "[INFO ]`t" -ForegroundColor White -NoNewline
    Write-Host "Script completed. Waiting $WaitSeconds seconds before closing..."
    for (`$i = $WaitSeconds; `$i -gt 0; `$i--) {
        Write-Host "`r[INFO ]`t" -ForegroundColor White -NoNewline
        Write-Host "Closing in `$i seconds... (Press any key to close immediately)" -NoNewline
        if ([Console]::KeyAvailable) {
            [Console]::ReadKey(`$true) | Out-Null
            break
        }
        Start-Sleep -Seconds 1
    }
    exit `$exitCode
} catch {
    Write-Host "[ERROR]`t" -ForegroundColor Red -NoNewline
    Write-Host "Script execution failed: `$_"
    Write-Host ""
    Write-Host "[INFO ]`t" -ForegroundColor White -NoNewline
    Write-Host "Press any key to close..."
    [Console]::ReadKey() | Out-Null
    exit 1
}
"@
        
        $tempScript = [System.IO.Path]::GetTempFileName() + ".ps1"
        $wrapperScript | Out-File -FilePath $tempScript -Encoding UTF8
        
        $argumentList = @(
            "-NoProfile"
            "-ExecutionPolicy Bypass"
            "-File `"$tempScript`""
        )
        
        $process = Start-Process -FilePath "powershell.exe" -ArgumentList $argumentList -Verb RunAs -PassThru -Wait
        
        # Clean up the temporary script
        Remove-Item -Path $tempScript -Force -ErrorAction SilentlyContinue
        
        if ($process.ExitCode -eq 0) {
            EchoInfo "Elevated script completed successfully"
        } else {
            EchoError "Elevated script failed with exit code: $($process.ExitCode)"
        }
        
        exit $process.ExitCode
    }
    catch {
        EchoError "Failed to elevate script: $_"
        exit 1
    }
}

