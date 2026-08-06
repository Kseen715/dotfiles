# session: x11+wayland
# modules/networkmanager.sh — NetworkManager + enabled service. ONE copy, POSIX
# (was .../modules/networkmanager.sh). lib32-libnm is multilib (needs the
# pacman-multilib module earlier); it is Arch-specific and pkgmap-passthrough.
# nm-applet is not cosmetic: it is the only thing that shows a wifi/VPN password
# prompt under a bare WM. nmtui/nmcli ship inside the NetworkManager package.
run_step "Installing NetworkManager" pkg_install \
    networkmanager network-manager-applet libnm lib32-libnm
enable_service NetworkManager
