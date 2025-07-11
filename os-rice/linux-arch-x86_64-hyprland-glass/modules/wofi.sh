info "Installing wofi..."
trace pacman -S --needed --noconfirm wofi
info "Installing wofi dotfiles..."
sudo -u "$DELEVATED_USER" mkdir -p /home/$DELEVATED_USER/.config/wofi
trace cp $SCRIPT_DIR/config/wofi/config /home/$DELEVATED_USER/.config/wofi/
trace cp $SCRIPT_DIR/config/wofi/style.css /home/$DELEVATED_USER/.config/wofi/