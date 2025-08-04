info "Installing dkms..."
trace pacman -S --needed --noconfirm dkms
# if 'linux' pkg is installed
if pacman -Qq | grep -qw linux; then
    trace pacman -S --needed --noconfirm linux-headers
fi
if pacman -Qq | grep -qw linux-lts; then
    trace pacman -S --needed --noconfirm linux-lts-headers
fi
if pacman -Qq | grep -qw linux-zen; then
    trace pacman -S --needed --noconfirm linux-zen-headers
fi
