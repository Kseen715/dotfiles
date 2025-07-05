info "Installing mako..."
trace pacman -S --needed --noconfirm mako
trace mkdir -p "/home/$DELEVATED_USER/.config/mako"
trace chmod 775 "/home/$DELEVATED_USER/.config/mako"
trace chown "$DELEVATED_USER":"$DELEVATED_USER" "/home/$DELEVATED_USER/.config/mako"
trace cp "$(dirname "$(realpath "$0")")/config/mako/config" "/home/$DELEVATED_USER/.config/mako/config"
trace chown "$DELEVATED_USER":"$DELEVATED_USER" "/home/$DELEVATED_USER/.config/mako/config"