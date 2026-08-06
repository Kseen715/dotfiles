# session: x11+wayland
# modules/amnezia-vpn.sh — AmneziaVPN client. ONE copy, POSIX
# (was .../apps/amnezia-vpn-client.sh). Maps amneziavpn -> aur:amneziavpn-bin on
# Arch; on apt -> source:provide_amneziavpn (upstream QtIFW installer, x86_64).
run_step "Installing AmneziaVPN" pkg_install amneziavpn
