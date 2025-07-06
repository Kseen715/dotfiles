info "Installing sddm..."
trace pacman -S --needed --noconfirm sddm qt6-5compat qt6-declarative qt6-svg
info "Installing sddm dotfiles..."
trace mkdir -p /etc/sddm.conf.d
trace cp $SCRIPT_DIR/config/sddm/hyprland.main.conf /etc/sddm.conf.d/sddm.conf
info "Installing sddm theme..." # config/sddm/glass-theme
trace mkdir -p /usr/share/sddm/themes/glass-theme
trace cp -r $SCRIPT_DIR/config/sddm/glass-theme /usr/share/sddm/themes
trace mkdir -p /etc/sddm.conf.d
trace cp $SCRIPT_DIR/config/sddm/theme.conf.user /etc/sddm.conf.d/theme.conf.user
info "Activating sddm..."
trace systemctl enable sddm.service --force 
