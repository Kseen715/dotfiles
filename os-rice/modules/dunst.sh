# session: x11+wayland
# modules/dunst.sh — dunst notification daemon, the X11 replacement for mako
# (i3-sugg §2). Config split (§5) uses dunst's own drop-in dir: the base dunstrc
# is dotfiles-owned (geometry, behaviour, mouse actions) and the rice drops
# ~/.config/dunst/dunstrc.d/90-theme.conf on top (colors, font, frame), which
# dunst merges in lexical order after the main file.

run_step "Installing dunst" pkg_install dunst libnotify

if [ -f "$OSR_DOTFILES/dunst/dunstrc" ]; then
    install_layer "$OSR_DOTFILES/dunst/dunstrc" "$OSR_HOME/.config/dunst/dunstrc"
fi

install_theme_layer dunst 90-theme.conf "$OSR_HOME/.config/dunst/dunstrc.d/90-theme.conf" || :
