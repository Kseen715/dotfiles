# Diagnostic Policy Service (DPS)
# Что делает: Анализирует проблемы Windows, запускает диагностические скрипты, собирает отчёты.

# Потребление: 20-40 МБ RAM, запускается при проблемах.

# Последствия: Встроенный диагностический инструмент Windows перестанет работать. Но если вы используете сторонние инструменты (например, HWiNFO, CrystalDiskInfo), это не проблема. Безопасно.

InvokeEcho "Stop-Service -Name DPS -Force"
InvokeEcho "Set-Service -Name DPS -StartupType Disabled"