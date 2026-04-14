info "Installing waylock..."
trace pacman -S --needed --noconfirm waylock
trace mkdir -p $DELEVATED_USER_HOME/.config/waylock
trace chmod 775 $DELEVATED_USER_HOME/.config/waylock
trace chown "$DELEVATED_USER":"$DELEVATED_USER" $DELEVATED_USER_HOME/.config/waylock
trace cp "$(dirname "$(realpath "$0")")/config/waylock/waylock.toml" $DELEVATED_USER_HOME/.config/waylock/
trace chmod 644 $DELEVATED_USER_HOME/.config/waylock/waylock.toml