# Windows Search (WSearch)
# Что делает: Индексирует все файлы на дисках для быстрого поиска. На SSD это не нужно, на HDD — сомнительно.

# Потребление: 100-500 МБ RAM, постоянная нагрузка на диск (30-50% активности).

# Последствия: Поиск в проводнике будет медленнее (ищет в реальном времени), но диск перестанет гудеть. На SSD разница в скорости поиска незаметна. Безопасно.

InvokeEcho "Stop-Service -Name WSearch -Force"
InvokeEcho "Remove-Item -Path '$env:ProgramData\Microsoft\Search\Data\Applications\Windows' -Recurse -Force -ErrorAction SilentlyContinue"
InvokeEcho "Set-Service -Name WSearch -StartupType Disabled"