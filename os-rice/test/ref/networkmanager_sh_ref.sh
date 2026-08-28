# test/ref/networkmanager_sh_ref.sh — the sh implementation of modules/networkmanager.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/networkmanager.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/networkmanager.sh — NetworkManager + enabled service. ONE copy, POSIX
# (was .../modules/networkmanager.sh). lib32-libnm is Arch multilib (needs the
# pacman-multilib module earlier); every other manager skips it via any.map, so
# the 64-bit stack is what actually installs off Arch.
# nm-applet is not cosmetic: it is the only thing that shows a wifi/VPN password
# prompt under a bare WM. nmtui/nmcli ship inside the NetworkManager package.
run_step "Installing NetworkManager" pkg_install \
    networkmanager network-manager-applet libnm lib32-libnm
enable_service NetworkManager
