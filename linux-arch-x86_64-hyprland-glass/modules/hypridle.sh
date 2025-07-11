info "Installing hypridle..."
trace pacman -S --needed --noconfirm hypridle
trace mkdir -p /home/$DELEVATED_USER/.config/hypr
trace chmod 775 /home/$DELEVATED_USER/.config/hypr
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /home/$DELEVATED_USER/.config/hypr
trace cp "$(dirname "$(realpath "$0")")/config/hypr/hypridle.conf" /home/$DELEVATED_USER/.config/hypr/
trace chmod 644 /home/$DELEVATED_USER/.config/hypr/hypridle.conf