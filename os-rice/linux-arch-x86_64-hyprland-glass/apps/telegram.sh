info "Installing Telegram..."
trace pacman -S --needed --noconfirm telegram-desktop webkit2gtk-4.1
trace mkdir -p /home/$DELEVATED_USER/.local/share/TelegramDesktop
trace chmod 775 /home/$DELEVATED_USER/.local/share/TelegramDesktop
trace chown -R $DELEVATED_USER:$DELEVATED_USER /home/$DELEVATED_USER/.local/share/TelegramDesktop
