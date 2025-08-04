info "Installing dkms..."
trace pacman -S --needed --noconfirm dkms
# if 'linux' pkg is installed
info "Checking for installed Linux kernel..."
if trace "pacman -Qq | grep -qw linux"; then
    info "Installing linux headers for dkms..."
    trace pacman -S --needed --noconfirm linux-headers
fi
if trace "pacman -Qq | grep -qw linux-lts"; then
    info "Installing linux-lts headers for dkms..."
    trace pacman -S --needed --noconfirm linux-lts-headers
fi
if trace "pacman -Qq | grep -qw linux-zen"; then
    info "Installing linux-zen headers for dkms..."
    trace pacman -S --needed --noconfirm linux-zen-headers
fi
