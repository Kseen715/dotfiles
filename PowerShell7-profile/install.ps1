param (
    [switch]$y = $false
)

$SEP = ","

$DIRECTORIES_TO_INSTALL = ""
$DIRECTORIES_LOCAL = ""

$FILES_TO_INSTALL = "$PROFILE"
$FILES_LOCAL = "./Microsoft.PowerShell_profile.ps1"

$YES = $y

# Convert comma-separated strings to arrays
$DIRS_TO_INSTALL = $DIRECTORIES_TO_INSTALL -split $SEP | ForEach-Object { $_.Trim() }
$DIRS_LOCAL = $DIRECTORIES_LOCAL -split $SEP | ForEach-Object { $_.Trim() }
$FILES_TO_INSTALL_ARR = $FILES_TO_INSTALL -split $SEP | ForEach-Object { $_.Trim() }
$FILES_LOCAL_ARR = $FILES_LOCAL -split $SEP | ForEach-Object { $_.Trim() }

# Process directories
for ($i = 0; $i -lt $DIRS_TO_INSTALL.Count; $i++) {
    $dir_to_save = $DIRS_TO_INSTALL[$i]
    $dir_local = $DIRS_LOCAL[$i]
    
    if (Test-Path -Path $dir_to_save -PathType Container) {
        if ($YES) {
            Copy-Item -Path $dir_local -Destination $dir_to_save -Recurse -Force
            Write-Host "[✨] Copied $dir_local to $dir_to_save"
        } else {
            Write-Host "[✨] Directory $dir_to_save already exists. Overwrite? (y/N)"
            $answer = Read-Host
            if ($answer -eq "y") {
                Copy-Item -Path $dir_local -Destination $dir_to_save -Recurse -Force
                Write-Host "[✨] Copied $dir_local to $dir_to_save"
            }
        }
    } else {
        $parentDir = Split-Path -Path $dir_to_save -Parent
        if (!(Test-Path -Path $parentDir)) {
            New-Item -Path $parentDir -ItemType Directory -Force | Out-Null
            Write-Host "[✨] Created directory $parentDir"
        }
        Copy-Item -Path $dir_local -Destination $dir_to_save -Recurse -Force
        Write-Host "[✨] Copied $dir_local to $dir_to_save"
    }
}

# Process files
for ($i = 0; $i -lt $FILES_TO_INSTALL_ARR.Count; $i++) {
    $file_to_save = $FILES_TO_INSTALL_ARR[$i]
    $file_local = $FILES_LOCAL_ARR[$i]
    
    if (Test-Path -Path $file_to_save -PathType Leaf) {
        if ($YES) {
            Copy-Item -Path $file_local -Destination $file_to_save -Force
            Write-Host "[✨] Copied $file_local to $file_to_save"
        } else {
            Write-Host "[✨] File $file_to_save already exists. Overwrite? (y/N)"
            $answer = Read-Host
            if ($answer -eq "y") {
                Copy-Item -Path $file_local -Destination $file_to_save -Force
                Write-Host "[✨] Copied $file_local to $file_to_save"
            }
        }
    } else {
        $parentDir = Split-Path -Path $file_to_save -Parent
        if (!(Test-Path -Path $parentDir)) {
            New-Item -Path $parentDir -ItemType Directory -Force | Out-Null
        }
        Copy-Item -Path $file_local -Destination $file_to_save -Force
        Write-Host "[✨] Copied $file_local to $file_to_save"
    }
}