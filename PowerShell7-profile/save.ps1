param (
    [switch]$y = $false
)

$SEP = ","

$DIRECTORIES_TO_SAVE = ""
$DIRECTORIES_LOCAL = ""

$FILES_TO_SAVE = "$PROFILE,$(Split-Path -Path $PROFILE -Parent)\ff-startup.jsonc"
$FILES_LOCAL = ".\Microsoft.PowerShell_profile.ps1,.\ff-startup.jsonc"

$YES = $y

# Convert comma-separated strings to arrays
$DIRS_TO_SAVE = @($DIRECTORIES_TO_SAVE -split $SEP | Where-Object { $_.Trim() -ne "" })
$DIRS_LOCAL = @($DIRECTORIES_LOCAL -split $SEP | Where-Object { $_.Trim() -ne "" })
$FILES_TO_SAVE_ARR = @($FILES_TO_SAVE -split $SEP | Where-Object { $_.Trim() -ne "" })
$FILES_LOCAL_ARR = @($FILES_LOCAL -split $SEP | Where-Object { $_.Trim() -ne "" })

# Process directories
if ($DIRS_TO_SAVE.Count -gt 0 -and $DIRS_LOCAL.Count -eq $DIRS_TO_SAVE.Count) {
    for ($i = 0; $i -lt $DIRS_TO_SAVE.Count; $i++) {
        $dir_to_save = $DIRS_TO_SAVE[$i]
        $dir_local = $DIRS_LOCAL[$i]
        
        if (!(Test-Path -Path $dir_to_save -PathType Container)) {
            Write-Host "[❌] Source directory $dir_to_save doesn't exist"
            continue
        }

        if (Test-Path -Path $dir_local -PathType Container) {
            if ($YES) {
                Copy-Item -Path "$dir_to_save\*" -Destination $dir_local -Recurse -Force
                Write-Host "[✨] Saved $dir_to_save to $dir_local"
            } else {
                Write-Host "[❓] Local directory $dir_local exists. Overwrite? (y/N)"
                $answer = Read-Host
                if ($answer -eq "y") {
                    Copy-Item -Path "$dir_to_save\*" -Destination $dir_local -Recurse -Force
                    Write-Host "[✨] Saved $dir_to_save to $dir_local"
                }
            }
        } else {
            New-Item -Path $dir_local -ItemType Directory -Force | Out-Null
            Copy-Item -Path "$dir_to_save\*" -Destination $dir_local -Recurse -Force
            Write-Host "[✨] Created and saved to $dir_local"
        }
    }
} elseif ($DIRS_TO_SAVE.Count -gt 0) {
    Write-Host "[❌] Directory arrays don't match in length. Please check your configuration."
}

# Process files
if ($FILES_TO_SAVE_ARR.Count -gt 0 -and $FILES_LOCAL_ARR.Count -eq $FILES_TO_SAVE_ARR.Count) {
    for ($i = 0; $i -lt $FILES_TO_SAVE_ARR.Count; $i++) {
        $file_to_save = $FILES_TO_SAVE_ARR[$i]
        $file_local = $FILES_LOCAL_ARR[$i]
        
        if (!(Test-Path -Path $file_to_save -PathType Leaf)) {
            Write-Host "[❌] Source file $file_to_save doesn't exist"
            continue
        }

        if (Test-Path -Path $file_local -PathType Leaf) {
            if ($YES) {
                Copy-Item -Path $file_to_save -Destination $file_local -Force
                Write-Host "[✨] Saved $file_to_save to $file_local"
            } else {
                Write-Host "[❓] Local file $file_local exists. Overwrite? (y/N)"
                $answer = Read-Host
                if ($answer -eq "y") {
                    Copy-Item -Path $file_to_save -Destination $file_local -Force
                    Write-Host "[✨] Saved $file_to_save to $file_local"
                }
            }
        } else {
            $parentDir = Split-Path -Path $file_local -Parent
            if (!(Test-Path -Path $parentDir)) {
                New-Item -Path $parentDir -ItemType Directory -Force | Out-Null
            }
            Copy-Item -Path $file_to_save -Destination $file_local -Force
            Write-Host "[✨] Saved $file_to_save to $file_local"
        }
    }
} elseif ($FILES_TO_SAVE_ARR.Count -gt 0) {
    Write-Host "[❌] File arrays don't match in length. Please check your configuration."
}
