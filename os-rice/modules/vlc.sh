# session: x11+wayland
# themable: yes
# modules/vlc.sh — VLC. The "plays anything, including the broken file" player;
# modules/celluloid.sh (mpv front-end) is the lighter one and they coexist fine.
#
# VLC's Qt interface follows QT_QPA_PLATFORMTHEME (modules/theming.sh sets the
# rice's Qt palette), so it inherits the desktop colors without a skin. Its own
# skin engine is a binary .vlt format and is deliberately not vendored here.
#
# The rice-owned vlcrc layer sets the interface to dark and turns off the
# playlist art fetching that otherwise phones home on every file.

run_step "Installing VLC" pkg_install vlc

install_theme_layer vlc vlcrc "$OSR_HOME/.config/vlc/vlcrc" || :
