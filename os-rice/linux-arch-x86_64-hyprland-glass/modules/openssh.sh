info "Installing openSSH..."
trace pacman -S --needed --noconfirm openssh
info "Enabling and starting sshd service..."
trace systemctl enable --now sshd.service