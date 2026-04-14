info "Installing wofi..."
trace pacman -S --needed --noconfirm wofi
info "Installing wofi dotfiles..."
sudo -u "$DELEVATED_USER" mkdir -p $DELEVATED_USER_HOME/.config/wofi
trace cp $SCRIPT_DIR/config/wofi/config $DELEVATED_USER_HOME/.config/wofi/
trace cp $SCRIPT_DIR/config/wofi/style.css $DELEVATED_USER_HOME/.config/wofi/