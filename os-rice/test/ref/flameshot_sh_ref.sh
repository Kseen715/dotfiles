# test/ref/flameshot_sh_ref.sh — the sh implementation of modules/flameshot.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/linux/flameshot.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11
# modules/flameshot.sh — screenshots, the X11 replacement for grim+slurp
# (i3-sugg §2). flameshot is the annotate-and-share GUI; maim+slop are the
# scriptable pair the i3 bindings use for "region straight to clipboard", which
# flameshot cannot do without opening its editor.

run_step "Installing screenshot tools" pkg_install flameshot maim slop xclip
