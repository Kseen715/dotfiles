info "Installing wezterm..."
trace pacman -S --needed --noconfirm wezterm
info "Installing dotfiles for wezterm..."
trace cd $DOTFILES_KSEEN715_REPO/wezterm
trace pacman -S --needed --noconfirm ttf-jetbrains-mono-nerd noto-fonts-emoji
trace chmod +x $DOTFILES_KSEEN715_REPO/wezterm/install.sh
trace $DOTFILES_KSEEN715_REPO/wezterm/install.sh -y
trace cd $SCRIPT_DIR