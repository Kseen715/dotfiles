info "Installing swaylock..."
trace pacman -S --needed --noconfirm swaylock
trace mkdir -p $DELEVATED_USER_HOME/.config/swaylock
trace chmod 775 $DELEVATED_USER_HOME/.config/swaylock
trace chown "$DELEVATED_USER":"$DELEVATED_USER" $DELEVATED_USER_HOME/.config/swaylock
trace cp "$(dirname "$(realpath "$0")")/config/swaylock/config" $DELEVATED_USER_HOME/.config/swaylock/
trace chmod 644 $DELEVATED_USER_HOME/.config/swaylock/config