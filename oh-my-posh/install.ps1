param (
    [switch]$y = $false
)

$SEP = ","

$DIRECTORIES_TO_INSTALL = ""
$DIRECTORIES_LOCAL = ""

$FILES_TO_INSTALL = "$env:POSH_THEMES_PATH\M365Princess++.omp.json"
$FILES_LOCAL = ".\M365Princess++.omp.json"

$YES = $y

# Convert comma-separated strings to arrays
$DIRS_TO_INSTALL = @($DIRECTORIES_TO_INSTALL -split $SEP | Where-Object { $_.Trim() -ne "" })
$DIRS_LOCAL = @($DIRECTORIES_LOCAL -split $SEP | Where-Object { $_.Trim() -ne "" })
$FILES_TO_INSTALL_ARR = @($FILES_TO_INSTALL -split $SEP | Where-Object { $_.Trim() -ne "" })
$FILES_LOCAL_ARR = @($FILES_LOCAL -split $SEP | Where-Object { $_.Trim() -ne "" })

# Process directories
if ($DIRS_LOCAL.Count -gt 0 -and $DIRS_TO_INSTALL.Count -eq $DIRS_LOCAL.Count) {
    for ($i = 0; $i -lt $DIRS_LOCAL.Count; $i++) {
        $dir_local = $DIRS_LOCAL[$i]
        $dir_to_install = $DIRS_TO_INSTALL[$i]
        
        if (!(Test-Path -Path $dir_local -PathType Container)) {
            Write-Host "[❌] Source directory $dir_local doesn't exist"
            continue
        }

        if (Test-Path -Path $dir_to_install -PathType Container) {
            if ($YES) {
                Copy-Item -Path "$dir_local\*" -Destination $dir_to_install -Recurse -Force
                Write-Host "[✨] Installed from $dir_local to $dir_to_install"
            } else {
                Write-Host "[❓] Destination directory $dir_to_install exists. Overwrite? (y/N) " -NoNewline
                $answer = Read-Host
                if ($answer -eq "y") {
                    Copy-Item -Path "$dir_local\*" -Destination $dir_to_install -Recurse -Force
                    Write-Host "[✨] Installed from $dir_local to $dir_to_install"
                }
            }
        } else {
            New-Item -Path $dir_to_install -ItemType Directory -Force | Out-Null
            Copy-Item -Path "$dir_local\*" -Destination $dir_to_install -Recurse -Force
            Write-Host "[✨] Created and installed to $dir_to_install"
        }
    }
} elseif ($DIRS_LOCAL.Count -gt 0) {
    Write-Host "[❌] Directory arrays don't match in length. Please check your configuration."
}

# Process files
if ($FILES_LOCAL_ARR.Count -gt 0 -and $FILES_TO_INSTALL_ARR.Count -eq $FILES_LOCAL_ARR.Count) {
    for ($i = 0; $i -lt $FILES_LOCAL_ARR.Count; $i++) {
        $file_local = $FILES_LOCAL_ARR[$i]
        $file_to_install = $FILES_TO_INSTALL_ARR[$i]
        
        if (!(Test-Path -Path $file_local -PathType Leaf)) {
            Write-Host "[❌] Source file $file_local doesn't exist"
            continue
        }

        if (Test-Path -Path $file_to_install -PathType Leaf) {
            if ($YES) {
                Copy-Item -Path $file_local -Destination $file_to_install -Force
                Write-Host "[✨] Installed $file_local to $file_to_install"
            } else {
                Write-Host "[❓] Destination file $file_to_install exists. Overwrite? (y/N) " -NoNewline
                $answer = Read-Host
                if ($answer -eq "y") {
                    Copy-Item -Path $file_local -Destination $file_to_install -Force
                    Write-Host "[✨] Installed $file_local to $file_to_install"
                }
            }
        } else {
            $parentDir = Split-Path -Path $file_to_install -Parent
            if (!(Test-Path -Path $parentDir)) {
                New-Item -Path $parentDir -ItemType Directory -Force | Out-Null
            }
            Copy-Item -Path $file_local -Destination $file_to_install -Force
            Write-Host "[✨] Installed $file_local to $file_to_install"
        }
    }
} elseif ($FILES_LOCAL_ARR.Count -gt 0) {
    Write-Host "[❌] File arrays don't match in length. Please check your configuration."
}