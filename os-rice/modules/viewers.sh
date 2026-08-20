# session: x11+wayland
# themable: yes
# modules/viewers.sh — the small openers ~/.config/mimeapps.list points at
# (modules/xdg.sh seeds that file). Without them "Open With" names applications
# that are not installed, which is worse than an empty menu: the double-click
# does nothing and no error is shown.
#
#   zathura + zathura-pdf-mupdf   PDF, keyboard-driven, themed from a plain rc
#   nsxiv                         images; reads its colors from X resources, so
#                                 modules/theming.sh already themed it
#   imv                           the Wayland-capable image viewer
#   mpv                           video/audio, and what celluloid wraps
#
# zathura is the only one with a config file worth owning: its rc is a flat
# `set option value` list, so the rice ships the color half as a layer that the
# base rc includes.

run_step "Installing document + media viewers" pkg_install \
    zathura zathura-pdf-mupdf nsxiv imv mpv

if [ -f "$OSR_DOTFILES/zathura/zathurarc" ]; then
    install_layer "$OSR_DOTFILES/zathura/zathurarc" "$OSR_HOME/.config/zathura/zathurarc"
fi
install_theme_layer zathura 90-theme.rc "$OSR_HOME/.config/zathura/90-theme.rc" || :

if [ -f "$OSR_DOTFILES/mpv/mpv.conf" ]; then
    install_layer "$OSR_DOTFILES/mpv/mpv.conf" "$OSR_HOME/.config/mpv/mpv.conf"
fi
install_theme_layer mpv 90-theme.conf "$OSR_HOME/.config/mpv/90-theme.conf" || :
