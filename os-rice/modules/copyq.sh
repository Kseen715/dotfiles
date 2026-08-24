# session: x11
# themable: yes
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/copyq.sh — clipboard manager, the X11 replacement for cliphist
# (i3-sugg §2.1). Not optional on X11: a selection is owned by the process that
# made it, so closing the source app destroys what you copied. CopyQ owns the
# selection on everyone's behalf.
#
# Void spells it CopyQ (xbps.map carries the row); xsel covers the PRIMARY
# selection for scripts that expect it.

run_step "Installing CopyQ" pkg_install copyq xclip xsel

# CopyQ paints its own item list from a theme .ini, not from the Qt palette
# (theme-owned, §6b). Installed as a loadable preset under themes/ - CopyQ keeps
# the ACTIVE appearance inside copyq.conf, which is user territory here, so this
# is applied once from Preferences > Appearance > Load and then swaps with the
# theme on every later switch.
install_theme_layer copyq theme.ini "$OSR_HOME/.config/copyq/themes/osr.ini" || :
