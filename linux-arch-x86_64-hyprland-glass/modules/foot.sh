info "Installing foot..."
trace pacman -S --needed --noconfirm foot
info "Installing foot dotfiles..."
sudo -u "$DELEVATED_USER" mkdir -p /home/$DELEVATED_USER/.config/foot
trace cp $SCRIPT_DIR/config/foot/foot.ini /home/$DELEVATED_USER/.config/foot/