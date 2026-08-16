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
    "$OSR_HOME/.local/share/color-schemes/os-rice.colors" || :
# The SAME file again as ~/.config/kdeglobals, which is the palette KDE apps read
# when nothing selected a scheme for them. Off Plasma there is no Colors KCM to
# do the selecting, and a scheme sitting unselected in color-schemes/ changes
# nothing - this second copy is what actually colors Kate, Dolphin and Okular
# here. It also covers every KDE app that has no katerc-style setting of its own.
install_theme_layer kde color-scheme.colors "$OSR_HOME/.config/kdeglobals" || :
# Pre-rename copy: the scheme used to be osr.colors with a per-theme Name, which
# no static katerc could select. Left behind it just clutters the picker.
rm -f "$OSR_HOME/.local/share/color-schemes/osr.colors" \
      "$OSR_HOME/.local/share/color-schemes/osr.colors.bak"
install_theme_layer konsole osr.colorscheme \
    "$OSR_HOME/.local/share/konsole/osr.colorscheme" || :

# The text area is a SECOND theme system (KSyntaxHighlighting), independent of
# the Qt palette above: without this file Kate paints the chrome in the rice's
# colors and the code in stock Breeze. katerc selects both by the same name,
# `os-rice`: the KDE scheme's file id, this one's metadata name.
install_theme_layer kate osr.theme \
    "$OSR_HOME/.local/share/org.kde.syntax-highlighting/themes/osr.theme" || :
