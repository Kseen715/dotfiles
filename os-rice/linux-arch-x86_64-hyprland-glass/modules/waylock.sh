info "Installing waylock..."
trace pacman -S --needed --noconfirm waylock
trace mkdir -p /home/$DELEVATED_USER/.config/waylock
trace chmod 775 /home/$DELEVATED_USER/.config/waylock
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /home/$DELEVATED_USER/.config/waylock
trace cp "$(dirname "$(realpath "$0")")/config/waylock/waylock.toml" /home/$DELEVATED_USER/.config/waylock/
trace chmod 644 /home/$DELEVATED_USER/.config/waylock/waylock.toml
