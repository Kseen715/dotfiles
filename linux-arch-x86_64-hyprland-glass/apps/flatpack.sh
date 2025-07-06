info "Installing Flatpak..."
trace pacman -S --needed --noconfirm flatpak
trace flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo