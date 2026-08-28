# test/ref/amnezia-vpn_sh_ref.sh — the sh implementation of modules/amnezia-vpn.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/amnezia-vpn.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/amnezia-vpn.sh — AmneziaVPN client. ONE copy, POSIX
# (was .../apps/amnezia-vpn-client.sh). Maps amneziavpn -> aur:amneziavpn-bin on
# Arch; on apt -> source:provide_amneziavpn (upstream QtIFW installer, x86_64).
run_step "Installing AmneziaVPN" pkg_install amneziavpn
