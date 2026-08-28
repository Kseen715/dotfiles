# test/ref/qbittorrent_sh_ref.sh — the sh implementation of modules/qbittorrent.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/qbittorrent.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/qbittorrent.sh — qBittorrent. ONE copy, POSIX (was .../apps/qbittorrent.sh).
run_step "Installing qBittorrent" pkg_install qbittorrent
as_user mkdir -p "$OSR_HOME/.config/qBittorrent"
