info "Installing qBittorrent..."
trace pacman -S --needed --noconfirm qbittorrent
trace mkdir -p /home/$DELEVATED_USER/.config/qBittorrent
trace chown -R $DELEVATED_USER:$DELEVATED_USER /home/$DELEVATED_USER/.config/qBittorrent