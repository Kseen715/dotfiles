info "Installing kate..."
trace pacman -S --needed --noconfirm kate
trace mkdir -p /home/$DELEVATED_USER/.config/
trace chmod 775 /home/$DELEVATED_USER/.config/
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /home/$DELEVATED_USER/.config
trace cp "$(dirname "$(realpath "$0")")/config/kate/katerc" /home/$DELEVATED_USER/.config/
trace chmod 644 /home/$DELEVATED_USER/.config/katerc