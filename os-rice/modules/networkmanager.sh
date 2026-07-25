# modules/networkmanager.sh — NetworkManager + enabled service. ONE copy, POSIX
# (was .../modules/networkmanager.sh). lib32-libnm is multilib (needs the
# pacman-multilib module earlier); it is Arch-specific and pkgmap-passthrough.
run_step "Installing NetworkManager" pkg_install networkmanager libnm lib32-libnm
enable_service NetworkManager
