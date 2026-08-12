# os-rice/windows-rice/src/fonts.ps1 — Nerd Font install.
#
# Windows equivalent of the Linux lib/fonts.sh helper (osr_install_nerd_font):
# prefer a package manager's nerd-fonts bucket/package, fall back to a manual
# download-and-register from the upstream GitHub release when neither has it.
# Per-user install (no admin needed): fonts land in the user's Fonts shell
# folder via the Shell.Application COM copy, which registers them too.

function Install-NerdFont {
    param([string]$Name = "JetBrainsMono")

    Add-Type -AssemblyName System.Drawing
    $installed = (New-Object System.Drawing.Text.InstalledFontCollection).Families.Name
    if ($installed -match [regex]::Escape($Name)) {
        EchoInfo "$Name Nerd Font already installed, skipping"
        return
    }

    if (Test-Command scoop) {
        EchoInfo "Installing $Name Nerd Font via scoop..."
        InvokeEcho "scoop bucket add nerd-fonts"
        InvokeEcho "scoop install nerd-fonts/$Name-NF"
        return
    }

    if (Test-Command choco) {
        EchoInfo "Installing $Name Nerd Font via choco..."
        InvokeEcho "choco install nerd-fonts-$($Name.ToLower()) -y"
        return
    }

    EchoWarning "No scoop/choco available; downloading $Name Nerd Font from GitHub..."
    $release = Invoke-RestMethod "https://api.github.com/repos/ryanoasis/nerd-fonts/releases/latest"
    $asset = $release.assets | Where-Object { $_.name -eq "$Name.zip" } | Select-Object -First 1
    if (-not $asset) {
        EchoError "Could not find $Name.zip in the latest nerd-fonts release"
        return
    }

    $tmpZip = Join-Path $env:TEMP "$Name-nerdfont.zip"
    $tmpDir = Join-Path $env:TEMP "$Name-nerdfont"
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $tmpZip
    Expand-Archive -Path $tmpZip -DestinationPath $tmpDir -Force

    # 0x14 = the per-user Fonts special folder; CopyHere with 0x10 (no UI) both
    # copies the file and registers it, same trick the manual double-click
    # install does.
    $fontsFolder = (New-Object -ComObject Shell.Application).Namespace(0x14)
    Get-ChildItem -Path $tmpDir -Filter "*.ttf" | ForEach-Object {
        $fontsFolder.CopyHere($_.FullName, 0x10)
    }

    Remove-Item -Recurse -Force $tmpZip, $tmpDir -ErrorAction SilentlyContinue
    EchoInfo "$Name Nerd Font installed"
}
