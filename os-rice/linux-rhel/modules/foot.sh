info "Installing foot..."
install_pkg_dnf foot

info "Installing foot dotfiles..."

# download and install jetbrains mono nerd font
FONT_DIR="/home/$DELEVATED_USER/.local/share/fonts"
trace sudo -u "$DELEVATED_USER" mkdir -p "$FONT_DIR"
check_error $? "Failed to create font directory $FONT_DIR"

# check if JetBrains Mono Nerd Font is already installed
if sudo -u "$DELEVATED_USER" fc-list | grep -q "JetBrainsMonoNerdFont"; then
    info "JetBrains Mono Nerd Font is already installed"
else
    trace sudo -u "$DELEVATED_USER" curl -L -o /tmp/JetBrainsMono.zip https://github.com/ryanoasis/nerd-fonts/releases/download/v3.4.0/JetBrainsMono.zip
    check_error $? "Failed to download JetBrains Mono Nerd Font"

    trace sudo -u "$DELEVATED_USER" unzip -o /tmp/JetBrainsMono.zip -d "$FONT_DIR"
    check_error $? "Failed to unzip JetBrains Mono Nerd Font to $FONT_DIR"

    trace sudo -u "$DELEVATED_USER" fc-cache -fv
    check_error $? "Failed to update font cache"
fi

# install foot config files
FOOT_CONFIG_DIR="/home/$DELEVATED_USER/.config/foot"
trace sudo -u "$DELEVATED_USER" mkdir -p "$FOOT_CONFIG_DIR"
check_error $? "Failed to create foot config directory $FOOT_CONFIG_DIR"

trace cp $SCRIPT_DIR/config/foot/foot-colors.ini "$FOOT_CONFIG_DIR"
check_error $? "Failed to copy foot-colors.ini to $FOOT_CONFIG_DIR"

trace cp $DOTFILES_KSEEN715_REPO/foot/foot.ini "$FOOT_CONFIG_DIR"
check_error $? "Failed to copy foot.ini to $FOOT_CONFIG_DIR"
