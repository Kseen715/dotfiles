# test/ref/redshift_sh_ref.sh — the sh implementation of modules/redshift.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/redshift.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11
# modules/redshift.sh — color temperature, the X11 replacement for luminance /
# gammastep (i3-sugg §2). Config is dotfiles-owned: it is a personal preference
# (latitude, day/night temperature), not a rice theme, so a rice switch leaves
# it alone.
#
# Without a location redshift refuses to start, so the shipped config sets one
# explicitly rather than relying on geoclue, which needs a D-Bus provider that
# i3 does not run.

run_step "Installing redshift" pkg_install redshift

if [ -f "$OSR_DOTFILES/redshift/redshift.conf" ]; then
    install_layer "$OSR_DOTFILES/redshift/redshift.conf" "$OSR_HOME/.config/redshift.conf"
fi
