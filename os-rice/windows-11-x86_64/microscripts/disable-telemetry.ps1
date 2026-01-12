# Connected User Experiences and Telemetry (DiagTrack)
# Что делает: Собирает данные об использовании ПК, приложениях, производительности, ошибках и отправляет в Microsoft. Это основной шпион в системе.

# Потребление: 50-150 МБ RAM, 5-10% CPU при активной отправке, постоянный сетевой трафик.

# Последствия: Нулевые. Система работает точно так же, просто Microsoft не получает ваши данные. Это абсолютно безопасно.

InvokeEcho "Stop-Service -Name DiagTrack -Force"
InvokeEcho "Set-Service -Name DiagTrack -StartupType Disabled"
InvokeEcho "Remove-Item -Path '$env:ProgramData\Microsoft\Diagnosis\ETLLogs' -Recurse -Force -ErrorAction SilentlyContinue"