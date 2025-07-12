info "Installing VSCode Insiders..."
trace sudo -u "$DELEVATED_USER" $AUR_HELPER -S --needed --noconfirm visual-studio-code-insiders-bin
trace pacman -S --needed --noconfirm ttf-cascadia-code-nerd ttf-cascadia-mono-nerd ttf-iosevkaterm-nerd