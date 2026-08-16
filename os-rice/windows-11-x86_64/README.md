# windows-11-x86_64

OS debloat/tweaks only (telemetry, search, taskbar, snap behavior, `sudo`).
Registry/service edits via `setup.ps1`, needs elevation, self-elevates via
`Invoke-ElevatedScript` (`src/common.ps1`).

For the WezTerm + oh-my-posh + PowerShell 7 rice (apps, fonts, dotfiles-owned
config), see [`../osr.ps1`](../osr.ps1) — a separate, unrelated concern that
needs no elevation.
