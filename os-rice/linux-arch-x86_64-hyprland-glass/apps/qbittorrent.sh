info "Installing qBittorrent..."
trace pacman -S --needed --noconfirm qbittorrent
trace mkdir -p "$DELEVATED_USER_HOME/.config/qBittorrent"
trace chown -R "$DELEVATED_USER":"$DELEVATED_USER" "$DELEVATED_USER_HOME/.config/qBittorrent"