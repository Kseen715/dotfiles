info "Installing mako..."
trace pacman -S --needed --noconfirm mako
trace mkdir -p "$DELEVATED_USER_HOME/.config/mako"
trace chmod 775 "$DELEVATED_USER_HOME/.config/mako"
trace chown "$DELEVATED_USER":"$DELEVATED_USER" "$DELEVATED_USER_HOME/.config/mako"
trace cp $SCRIPT_DIR/config/mako/config "$DELEVATED_USER_HOME/.config/mako/config"
trace chown "$DELEVATED_USER":"$DELEVATED_USER" "$DELEVATED_USER_HOME/.config/mako/config"