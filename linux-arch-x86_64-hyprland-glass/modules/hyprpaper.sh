info "Installing hyprpaper..."
trace pacman -S --needed --noconfirm hyprpaper
info "Installing hyprpaper dotfiles..."
sudo -u "$DELEVATED_USER" mkdir -p /home/$DELEVATED_USER/.config/hypr
trace cp config/hypr/hyprpaper.conf /home/$DELEVATED_USER/.config/hypr/
mkdir -p /home/$DELEVATED_USER/Pictures/Wallpapers
chown "$DELEVATED_USER":"$DELEVATED_USER" /home/$DELEVATED_USER/Pictures/Wallpapers/
chmod 775 /home/$DELEVATED_USER/Pictures/Wallpapers/
trace "sudo -u $DELEVATED_USER cp \"wallpapers/avogado6 - 2024.06.jpg\" \"/home/$DELEVATED_USER/Pictures/Wallpapers/avogado6 - 2024.06.jpg\""