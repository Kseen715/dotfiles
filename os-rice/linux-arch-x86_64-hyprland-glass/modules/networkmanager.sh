info "Installing NetworkManager..."
trace pacman -S --needed --noconfirm networkmanager libnm lib32-libnm
trace systemctl enable --now NetworkManager.service