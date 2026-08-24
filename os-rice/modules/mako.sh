# session: wayland
# themable: yes
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/mako.sh — mako notification daemon + rice-owned config. ONE copy, POSIX
# (was .../modules/mako.sh).
run_step "Installing mako" pkg_install mako
install_theme_layer mako config "$OSR_HOME/.config/mako/config" || :
