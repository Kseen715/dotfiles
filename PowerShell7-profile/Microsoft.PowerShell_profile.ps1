# PowerShell profile script
# Big thanks to Scott Hanselman (@shanselman) for this kick-ass script
# Tweaked & enhanced by Ibne Nahian (@evilprince2009)
# Tweaked & enhanced by Denis Korenev (@Kseen715)

# REQUIRES:
# - choco
# - oh-my-posh (get it from choco)
# - fastfetch (get it from choco)
# - config file for fastfetch


# Run `$profile` in PS to get place where you need to put this file 
# and fastfetch config


# Hotkeys:
# - Alt+D           - Deletes the word to the right of the cursor
# - Alt+Backspace   - Deletes the word to the left of the cursor
# - F1              - Show PS help for the written command
# - RightArrow      - Move cursor one character to the right in the current 
#                       editing line and accept the next word in suggestion 
#                       when it's at the end of current editing line
# - Alt+A           - Set current selection to next command argument in the 
#                       command line. Use of digit argument selects argument 
#                       by position

# These hotkeys doesn't work in WindowsTerminal:
# - Ctrl+Shift+J    - Mark the current directory
# - Ctrl+j          - Goto the marked directory
# - Alt+J           - Show the currently marked directories

using namespace System.Management.Automation
using namespace System.Management.Automation.Language

$profileFolder = Split-Path -Parent $profile
$ffConfigPath = Join-Path -Path $profileFolder -ChildPath 'ff-startup.jsonc'

if ($host.Name -eq 'ConsoleHost') {
    Import-Module -Name PSReadLine
}
# Import-Module -Name posh-git
# Import-Module -Name z
# Import-Module -Name Terminal-Icons
<#
    # Enable these Import statements if you want to use omp v2 & disable line 20
    # Import-Module -Name oh-my-posh
    # Set-PoshPrompt -Theme iterm2
#>

oh-my-posh --init --shell pwsh --config ~\AppData\Local\Programs\oh-my-posh\themes\M365Princess.omp.json | Invoke-Expression


Register-ArgumentCompleter -Native -CommandName winget -ScriptBlock {
    param($wordToComplete, $commandAst, $cursorPosition)
    [Console]::InputEncoding = [Console]::OutputEncoding = $OutputEncoding = [System.Text.Utf8Encoding]::new()
    $Local:word = $wordToComplete.Replace('"', '""')
    $Local:ast = $commandAst.ToString().Replace('"', '""')
    winget complete --word="$Local:word" --commandline "$Local:ast" --position $cursorPosition | ForEach-Object {
        [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', $_)
    }
}

Register-ArgumentCompleter -Native -CommandName dotnet -ScriptBlock {
    param($commandName, $wordToComplete, $cursorPosition)
    dotnet complete --position $cursorPosition "$wordToComplete" | ForEach-Object {
        [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', $_)
    }
}

# History search
Set-PSReadLineKeyHandler -Key UpArrow -Function HistorySearchBackward
Set-PSReadLineKeyHandler -Key DownArrow -Function HistorySearchForward

Set-PSReadLineKeyHandler -Key Alt+d -Function ShellKillWord
Set-PSReadLineKeyHandler -Key Alt+Backspace -Function ShellBackwardKillWord

Set-PSReadLineKeyHandler -Key F1 `
    -BriefDescription CommandHelp `
    -LongDescription "Open the help window for the current command" `
    -ScriptBlock {
    param($key, $arg)

    $ast = $null
    $tokens = $null
    $errors = $null
    $cursor = $null
    [Microsoft.PowerShell.PSConsoleReadLine]::GetBufferState([ref]$ast, [ref]$tokens, [ref]$errors, [ref]$cursor)

    $commandAst = $ast.FindAll( {
            $node = $args[0]
            $node -is [CommandAst] -and
            $node.Extent.StartOffset -le $cursor -and
            $node.Extent.EndOffset -ge $cursor
        }, $true) | Select-Object -Last 1

    if ($commandAst -ne $null) {
        $commandName = $commandAst.GetCommandName()
        if ($commandName -ne $null) {
            $command = $ExecutionContext.InvokeCommand.GetCommand($commandName, 'All')
            if ($command -is [AliasInfo]) {
                $commandName = $command.ResolvedCommandName
            }

            if ($commandName -ne $null) {
                Get-Help $commandName -ShowWindow
            }
        }
    }
}

$global:PSReadLineMarks = @{}


Set-PSReadLineKeyHandler -Key Ctrl+J `
    -BriefDescription MarkDirectory `
    -LongDescription "Mark the current directory" `
    -ScriptBlock {
    param($key, $arg)

    $key = [Console]::ReadKey($true)
    $global:PSReadLineMarks[$key.KeyChar] = $pwd
}

Set-PSReadLineKeyHandler -Key Ctrl+j `
    -BriefDescription JumpDirectory `
    -LongDescription "Goto the marked directory" `
    -ScriptBlock {
    param($key, $arg)

    $key = [Console]::ReadKey()
    $dir = $global:PSReadLineMarks[$key.KeyChar]
    if ($dir) {
        Set-Location $dir
        [Microsoft.PowerShell.PSConsoleReadLine]::InvokePrompt()
    }
}

Set-PSReadLineKeyHandler -Key Alt+j `
    -BriefDescription ShowDirectoryMarks `
    -LongDescription "Show the currently marked directories" `
    -ScriptBlock {
    param($key, $arg)

    $global:PSReadLineMarks.GetEnumerator() | ForEach-Object {
        [PSCustomObject]@{Key = $_.Key; Dir = $_.Value } } |
    Format-Table -AutoSize | Out-Host

    [Microsoft.PowerShell.PSConsoleReadLine]::InvokePrompt()
}

Set-PSReadLineKeyHandler -Key RightArrow `
    -BriefDescription ForwardCharAndAcceptNextSuggestionWord `
    -LongDescription "Move cursor one character to the right in the current editing line and accept the next word in suggestion when it's at the end of current editing line" `
    -ScriptBlock {
    param($key, $arg)

    $line = $null
    $cursor = $null
    [Microsoft.PowerShell.PSConsoleReadLine]::GetBufferState([ref]$line, [ref]$cursor)

    if ($cursor -lt $line.Length) {
        [Microsoft.PowerShell.PSConsoleReadLine]::ForwardChar($key, $arg)
    }
    else {
        [Microsoft.PowerShell.PSConsoleReadLine]::AcceptNextSuggestionWord($key, $arg)
    }
}

Set-PSReadLineKeyHandler -Key Alt+a `
    -BriefDescription SelectCommandArguments `
    -LongDescription "Set current selection to next command argument in the command line. Use of digit argument selects argument by position" `
    -ScriptBlock {
    param($key, $arg)
  
    $ast = $null
    $cursor = $null
    [Microsoft.PowerShell.PSConsoleReadLine]::GetBufferState([ref]$ast, [ref]$null, [ref]$null, [ref]$cursor)
  
    $asts = $ast.FindAll( {
            $args[0] -is [System.Management.Automation.Language.ExpressionAst] -and
            $args[0].Parent -is [System.Management.Automation.Language.CommandAst] -and
            $args[0].Extent.StartOffset -ne $args[0].Parent.Extent.StartOffset
        }, $true)
  
    if ($asts.Count -eq 0) {
        [Microsoft.PowerShell.PSConsoleReadLine]::Ding()
        return
    }
    
    $nextAst = $null

    if ($null -ne $arg) {
        $nextAst = $asts[$arg - 1]
    }
    else {
        foreach ($ast in $asts) {
            if ($ast.Extent.StartOffset -ge $cursor) {
                $nextAst = $ast
                break
            }
        } 
        
        if ($null -eq $nextAst) {
            $nextAst = $asts[0]
        }
    }

    $startOffsetAdjustment = 0
    $endOffsetAdjustment = 0

    if ($nextAst -is [System.Management.Automation.Language.StringConstantExpressionAst] -and
        $nextAst.StringConstantType -ne [System.Management.Automation.Language.StringConstantType]::BareWord) {
        $startOffsetAdjustment = 1
        $endOffsetAdjustment = 2
    }
  
    [Microsoft.PowerShell.PSConsoleReadLine]::SetCursorPosition($nextAst.Extent.StartOffset + $startOffsetAdjustment)
    [Microsoft.PowerShell.PSConsoleReadLine]::SetMark($null, $null)
    [Microsoft.PowerShell.PSConsoleReadLine]::SelectForwardChar($null, ($nextAst.Extent.EndOffset - $nextAst.Extent.StartOffset) - $endOffsetAdjustment)
}


Set-PSReadLineOption -PredictionSource HistoryAndPlugin
Set-PSReadLineOption -PredictionViewStyle ListView
Set-PSReadLineOption -EditMode Windows

Set-PSReadLineOption -Colors @{ InlinePrediction = ‘#8A0303’}
Set-PSReadLineOption -Colors @{ InlinePrediction = ‘#2F7004’}
Set-PSReadLineOption -Colors @{ InlinePrediction = “$([char]0x1b)[36;7;238m”}

<#
    # Below command requires installing DotFetch properly to execute
    # DotFetch original repo: https://github.com/evilprince2009/DotFetch
#>
# Import the Chocolatey Profile that contains the necessary code to enable
# tab-completions to function for `choco`.
# Be aware that if you are missing these lines from your profile, tab completion
# for `choco` will not function.
# See https://ch0.co/tab-completion for details.
# $ChocolateyProfile = "$env:ChocolateyInstall\helpers\chocolateyProfile.psm1"
# if (Test-Path($ChocolateyProfile)) {
#   Import-Module "$ChocolateyProfile"
# }

cls
fastfetch -c $ffConfigPath