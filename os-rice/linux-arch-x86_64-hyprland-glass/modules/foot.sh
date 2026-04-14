info "Installing foot..."
trace pacman -S --needed --noconfirm foot
info "Installing foot dotfiles..."
sudo -u "$DELEVATED_USER" mkdir -p $DELEVATED_USER_HOME/.config/foot
trace cp $SCRIPT_DIR/config/foot/foot-colors.ini $DELEVATED_USER_HOME/.config/foot/
trace cp $DOTFILES_KSEEN715_REPO/foot/foot.ini $DELEVATED_USER_HOME/.config/foot/