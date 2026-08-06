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
