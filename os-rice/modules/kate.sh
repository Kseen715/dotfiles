# session: x11+wayland
# modules/kate.sh — Kate text editor + dotfiles config. ONE copy, POSIX
# (was .../apps/kate.sh). katerc is dotfiles-owned config (§5).
run_step "Installing Kate" pkg_install kate
if [ -f "$OSR_DOTFILES/kate/katerc" ]; then
    install_layer "$OSR_DOTFILES/kate/katerc" "$OSR_HOME/.config/katerc"
fi
