# test/ref/dunst_sh_ref.sh — the sh implementation of modules/dunst.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/dunst.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# themable: yes
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
