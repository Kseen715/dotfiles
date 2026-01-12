# dmwappushservice (WAP Push Service)
# Что делает: Поддерживает WAP-протокол для пуш-уведомлений от Microsoft. Используется для рекламы в меню Пуск и "рекомендаций" в проводнике.

# Потребление: 10-15 МБ RAM, постоянный фоновый процесс.

# Последствия: Реклама и рекомендации в меню Пуск исчезнут. Это плюс, а не минус.

InvokeEcho "Stop-Service -Name dmwappushservice -Force"
InvokeEcho "Set-Service -Name dmwappushservice -StartupType Disabled"