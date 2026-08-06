# session: x11+wayland
# modules/telegram.sh — Telegram Desktop. ONE copy, POSIX (was .../apps/telegram.sh).
# webkit2gtk-4.1 is a companion lib the legacy pulled alongside it.
run_step "Installing Telegram" pkg_install telegram-desktop webkit2gtk-4.1
as_user mkdir -p "$OSR_HOME/.local/share/TelegramDesktop"
