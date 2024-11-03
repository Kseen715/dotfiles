# Define the source paths
$settingsPath = "$env:APPDATA\Code - Insiders\User\settings.json"
$keybindingsPath = "$env:APPDATA\Code - Insiders\User\keybindings.json"
$extensionsPath = "$env:USERPROFILE\.vscode-insiders\extensions\extensions.json"
$snippetsPath = "$env:APPDATA\Code - Insiders\User\snippets"

# Define the destination folder (current folder)
$destinationFolder = Get-Location

# Copy the files to the destination folder
Copy-Item -Path $settingsPath -Destination "$destinationFolder\User" -Force
Copy-Item -Path $keybindingsPath -Destination "$destinationFolder\User" -Force
Copy-Item -Path $extensionsPath -Destination "$destinationFolder\extensions" -Force
Copy-Item -Path "$snippetsPath\*" -Destination "$destinationFolder\snippets" -Recurse -Force

Write-Output "Files have been copied to $destinationFolder"