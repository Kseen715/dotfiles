# add multilib repository to pacman.conf if not already present (can be commented out, if so - add it anyway)
if ! grep -q "^\[multilib\]" /etc/pacman.conf; then
    info "Adding multilib repository to /etc/pacman.conf"
    trace sudo tee -a /etc/pacman.conf <<EOF

[multilib]
Include = /etc/pacman.d/mirrorlist
EOF
else
    info "Multilib repository already exists in /etc/pacman.conf"
fi
trace pacman -Sy