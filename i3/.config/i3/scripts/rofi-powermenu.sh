#!/bin/sh
# rofi-powermenu.sh — the X11 replacement for wleave/wlogout (i3-sugg §2).
# Ten lines and a rofi theme instead of another package and another GTK layer.
#
# Poweroff/reboot go through loginctl when available (works unprivileged via
# polkit on both systemd and elogind) and fall back to the plain commands, which
# is what a runit box without a polkit rule needs.
set -eu

_choice=$(printf 'lock\nlogout\nsuspend\nreboot\nshutdown\n' \
    | rofi -dmenu -i -p 'power' -theme ~/.config/rofi/powermenu.rasi)

case "$_choice" in
    lock)     betterlockscreen -l dimblur ;;
    logout)   i3-msg exit ;;
    suspend)  betterlockscreen -l dimblur & loginctl suspend 2>/dev/null || zzz 2>/dev/null || true ;;
    reboot)   loginctl reboot 2>/dev/null || reboot ;;
    shutdown) loginctl poweroff 2>/dev/null || poweroff ;;
esac
