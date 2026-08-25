#!/bin/sh
# rofi-powermenu.sh — the X11 replacement for wleave/wlogout (i3-sugg §2).
# Opened by $mod+Shift+e and by the bar's power button (polybar modules.ini),
# so this is the one power menu in the session.
#
# Laid out after the glass rice's wleave: a row of rounded cards, each a large
# centered icon over a small label, with the accent filling the focused one.
# The icons are Nerd Font glyphs rather than wleave's PNGs on purpose - glass
# ships lock.png/power.png/... as white bitmaps, which cannot follow a palette,
# and every color in this menu comes from the rice's colors.rasi. Same look, and
# it survives a theme switch.
#
# Poweroff/reboot go through loginctl when available (works unprivileged via
# polkit on both systemd and elogind) and fall back to the plain commands, which
# is what a runit box without a polkit rule needs.
set -eu

# Entries are separated by | rather than newline (-sep below), because each one
# CONTAINS a newline: icon on the first line, label on the second.
#
# -eh 3, not 2. `-eh N` sizes the row as N times the BASE line height, and the
# icon line is 185% of it - so two lines of content do not fit in two lines of
# box and rofi clips the label instead of growing the row. Measured: at -eh 2
# every label lost its lower half.
_menu=$(printf '%s' \
'<span size="185%">󰌾</span>
<span size="75%">lock</span>|<span size="185%">󰍃</span>
<span size="75%">logout</span>|<span size="185%">󰤄</span>
<span size="75%">sleep</span>|<span size="185%">󰜉</span>
<span size="75%">reboot</span>|<span size="185%">󰐥</span>
<span size="75%">shutdown</span>')

# -format i returns the INDEX of the picked entry, not its text. That matters
# here: the entry text is Pango markup wrapped around a glyph, and comparing
# that back with `case` would tie the actions to the exact bytes of the label -
# a restyle would silently stop shutting the machine down.
# -no-show-icons matters and is not cosmetic. config.rasi sets `show-icons:
# true` for the launcher, and that is a CONFIGURATION option, so -theme does not
# override it: rofi allocates an element-icon slot in every row here too. These
# rows carry no icon file - the glyph is text - so the slot stays empty and just
# pushes the text off-centre. Measured against the card rectangles: the cards
# themselves sit correctly (19px inset, symmetric), but every icon+label sat
# ~10px right of its own card's centre, which is exactly what "the buttons are
# shifted right" looks like.
_choice=$(printf '%s' "$_menu" |
    rofi -dmenu -sep '|' -eh 3 -markup-rows -format i -no-show-icons \
         -p 'power' -theme "$HOME/.config/rofi/powermenu.rasi") || exit 0

case "$_choice" in
    0) betterlockscreen -l dimblur ;;
    1) i3-msg exit ;;
    2) betterlockscreen -l dimblur & loginctl suspend 2>/dev/null || zzz 2>/dev/null || true ;;
    3) loginctl reboot 2>/dev/null || reboot ;;
    4) loginctl poweroff 2>/dev/null || poweroff ;;
esac
