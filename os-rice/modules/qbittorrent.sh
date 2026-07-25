# modules/qbittorrent.sh — qBittorrent. ONE copy, POSIX (was .../apps/qbittorrent.sh).
run_step "Installing qBittorrent" pkg_install qbittorrent
as_user mkdir -p "$OSR_HOME/.config/qBittorrent"
