# Fax (Fax)
# Что делает: Поддержка факсов. В 2025 году это рудимент.

# Потребление: 5-10 МБ RAM.

# Последствия: Никаких. Безопасно.

InvokeEcho "Stop-Service -Name fax -Force"
InvokeEcho "Set-Service -Name fax -StartupType Disabled"