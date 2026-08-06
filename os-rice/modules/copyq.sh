# session: x11
# modules/copyq.sh — clipboard manager, the X11 replacement for cliphist
# (i3-sugg §2.1). Not optional on X11: a selection is owned by the process that
# made it, so closing the source app destroys what you copied. CopyQ owns the
# selection on everyone's behalf.
#
# Void spells it CopyQ (xbps.map carries the row); xsel covers the PRIMARY
# selection for scripts that expect it.

run_step "Installing CopyQ" pkg_install copyq xclip xsel
