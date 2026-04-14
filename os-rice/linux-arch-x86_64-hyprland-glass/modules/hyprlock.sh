info "Installing hyprlock..."
trace pacman -S --needed --noconfirm hyprlock
trace mkdir -p $DELEVATED_USER_HOME/.config/hypr
trace chmod 775 $DELEVATED_USER_HOME/.config/hypr
trace chown "$DELEVATED_USER":"$DELEVATED_USER" $DELEVATED_USER_HOME/.config/hypr
trace cp "$(dirname "$(realpath "$0")")/config/hypr/hyprlock.conf" $DELEVATED_USER_HOME/.config/hypr/
trace chmod 644 $DELEVATED_USER_HOME/.config/hypr/hyprlock.conf