# session: x11+wayland
# modules/kate.sh — Kate text editor + dotfiles config. ONE copy, POSIX
# (was .../apps/kate.sh). katerc is dotfiles-owned config (§5).
run_step "Installing Kate" pkg_install kate
if [ -f "$OSR_DOTFILES/kate/katerc" ]; then
    install_layer "$OSR_DOTFILES/kate/katerc" "$OSR_HOME/.config/katerc"
fi

# KDE/Qt palette (theme-owned, §6b). Kate reads the color scheme every KDE app
# reads, so this file is what stops Kate being stock Breeze on a themed desktop;
# the Konsole scheme rides along because it is the same 16 colors in the same
# format, and lands harmlessly when Konsole is not installed.
install_theme_layer kde color-scheme.colors \
    "$OSR_HOME/.local/share/color-schemes/osr.colors" || :
install_theme_layer konsole osr.colorscheme \
    "$OSR_HOME/.local/share/konsole/osr.colorscheme" || :
