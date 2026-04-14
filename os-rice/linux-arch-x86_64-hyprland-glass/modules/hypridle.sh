info "Installing hypridle..."
trace pacman -S --needed --noconfirm hypridle
trace mkdir -p $DELEVATED_USER_HOME/.config/hypr
trace chmod 775 $DELEVATED_USER_HOME/.config/hypr
trace chown "$DELEVATED_USER":"$DELEVATED_USER" $DELEVATED_USER_HOME/.config/hypr
trace cp "$(dirname "$(realpath "$0")")/config/hypr/hypridle.conf" $DELEVATED_USER_HOME/.config/hypr/
trace chmod 644 $DELEVATED_USER_HOME/.config/hypr/hypridle.conf