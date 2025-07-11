info "Installing swaylock..."
trace pacman -S --needed --noconfirm swaylock
trace mkdir -p /home/$DELEVATED_USER/.config/swaylock
trace chmod 775 /home/$DELEVATED_USER/.config/swaylock
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /home/$DELEVATED_USER/.config/swaylock
trace cp "$(dirname "$(realpath "$0")")/config/swaylock/config" /home/$DELEVATED_USER/.config/swaylock/
trace chmod 644 /home/$DELEVATED_USER/.config/swaylock/config
