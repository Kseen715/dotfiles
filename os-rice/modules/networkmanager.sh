# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/networkmanager.sh — NetworkManager + enabled service. ONE copy, POSIX
# (was .../modules/networkmanager.sh). lib32-libnm is Arch multilib (needs the
# pacman-multilib module earlier); every other manager skips it via any.map, so
# the 64-bit stack is what actually installs off Arch.
# nm-applet is not cosmetic: it is the only thing that shows a wifi/VPN password
# prompt under a bare WM. nmtui/nmcli ship inside the NetworkManager package.
run_step "Installing NetworkManager" pkg_install \
    networkmanager network-manager-applet libnm lib32-libnm
enable_service NetworkManager
