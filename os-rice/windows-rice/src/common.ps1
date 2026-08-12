# os-rice/windows-rice/src/common.ps1 — shared, minimal echo/session helpers.
#
# ASCII-only output on purpose (mirrors ../../DESIGN.md's "CLI output is
# ASCII-only" decision on the Linux side): a bare UTF-8-no-BOM .ps1 read via
# `powershell.exe -File` gets decoded through the system's non-Unicode
# codepage, not UTF-8. On a non-Latin-1 codepage (e.g. a Cyrillic Windows
# install) that doesn't just mis-render — it can desync tokenization badly
# enough to corrupt unrelated logic later in the same file. ASCII bytes decode
# identically under every codepage, so this removes the whole bug class for
# free, same as it did on the POSIX side.

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

function Update-SessionEnvironment {
    # Re-reads User + Machine env vars from the registry into this process.
    # Needed because a package manager (scoop/choco/winget) that just installed
    # something (e.g. oh-my-posh setting POSH_THEMES_PATH) only writes the
    # registry -- the current session never sees it without this, normally
    # requiring a new shell window.
    foreach ($scope in 'Machine', 'User') {
        [Environment]::GetEnvironmentVariables($scope).GetEnumerator() | ForEach-Object {
            if ($_.Name -ieq 'Path') { return }
            Set-Item -Path "Env:$($_.Name)" -Value $_.Value -ErrorAction SilentlyContinue
        }
    }
    $machinePath = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = @($machinePath, $userPath) -join ';'
}
