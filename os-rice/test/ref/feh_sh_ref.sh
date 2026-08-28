# test/ref/feh_sh_ref.sh — the sh implementation of modules/feh.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/feh.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11
# modules/feh.sh — wallpaper setter, the X11 replacement for hyprpaper
# (i3-sugg §2). feh has no daemon: it paints the root window once and exits, so
# the i3 config re-runs it on every start. xcolor is the X11 hyprpicker.
#
# The wallpaper itself is resolved and installed by the shared §6 helper
# (apply_wallpaper at the end of a rice run) — this module only provides the
# setter, so nothing here hard-codes a path.

run_step "Installing feh + xcolor" pkg_install feh xcolor
