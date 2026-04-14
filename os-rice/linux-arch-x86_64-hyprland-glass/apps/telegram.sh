info "Installing Telegram..."
trace pacman -S --needed --noconfirm telegram-desktop webkit2gtk-4.1
trace mkdir -p $DELEVATED_USER_HOME/.local/share/TelegramDesktop
trace chmod 775 $DELEVATED_USER_HOME/.local/share/TelegramDesktop
trace chown -R "$DELEVATED_USER":"$DELEVATED_USER" $DELEVATED_USER_HOME/.local/share/TelegramDesktop