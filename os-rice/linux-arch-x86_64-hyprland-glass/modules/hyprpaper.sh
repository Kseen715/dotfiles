info "Installing hyprpaper..."
trace pacman -S --needed --noconfirm hyprpaper
info "Installing hyprpaper dotfiles..."
sudo -u "$DELEVATED_USER" mkdir -p "$DELEVATED_USER_HOME/.config/hypr"
trace cp $SCRIPT_DIR/config/hypr/hyprpaper.conf "$DELEVATED_USER_HOME/.config/hypr/"
mkdir -p "$DELEVATED_USER_HOME/Pictures/Wallpapers"
chown "$DELEVATED_USER":"$DELEVATED_USER" "$DELEVATED_USER_HOME/Pictures/Wallpapers/"
chmod 775 "$DELEVATED_USER_HOME/Pictures/Wallpapers/"
trace "sudo -u \"$DELEVATED_USER\" cp \"wallpapers/avogado6 - 2024.06.jpg\" \"$DELEVATED_USER_HOME/Pictures/Wallpapers/avogado6 - 2024.06.jpg\""