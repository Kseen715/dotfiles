#!/bin/sh
# i3/scripts/min-tile.sh — dotfiles-owned (os-rice §5). A floor under tile size:
# when a new window would make its container narrower or shorter than the floor,
# the container becomes TABBED instead of splitting again.
#
# i3 has no minimum tile size and will keep halving forever. Past a certain
# count that is not merely unreadable, it is degenerate: measured on a 1366x768
# panel with fourteen windows in one column, i3 handed out rects of 11x9, 7x4
# and then sizes that had underflowed entirely -
#
#   7x4294967289        (that is -7, as an unsigned 32-bit value)
#
# - and a window with no area is a window whose pixmap cannot be turned into a
# RENDER picture. picom takes the BadMatch and dies:
#
#   x_create_picture_with_pictfmt_and_pixmap ERROR ] failed to create picture
#   (X error 8 MATCH request 139 minor 4 serial 28509)
#
# so the whole session loses its compositor because one more terminal was
# opened. The compositor is supervised now (picom/launch.sh) and comes back,
# but coming back from a thing that should not have happened is not a fix.
#
# WHY TABBED AND NOT "REFUSE TO OPEN"
#
# Refusing is not available - the window already exists by the time anything
# can react to it - and floating it would put it over the workspace it was
# meant to join. Tabbing keeps every window on the workspace, full size, one
# keystroke apart; it is what a person does by hand at the point where columns
# stop being readable, so it is what this does at the point where they stop
# being valid.
#
# The floor is per-axis and only the offending axis is acted on, because a
# short-but-wide tile is a legitimate layout and a 7px-wide one is not.
set -u

# WHAT THIS DOES NOT DO
#
# The check is on the window that just appeared, not on its siblings: open a
# window that is itself fine and the tiles beside it can still end up under the
# floor. That is deliberate - reading every sibling means parsing i3's tree on
# every window event, and the floor exists to keep rects VALID, which is a
# property of the new window. Sizes that are merely tight stay tight.
MIN_W=${OSR_MIN_TILE_WIDTH:-240}
MIN_H=${OSR_MIN_TILE_HEIGHT:-120}

command -v i3-msg >/dev/null 2>&1 || exit 0
command -v xdotool >/dev/null 2>&1 || exit 0

# i3 runs this from exec_always, which fires again on every reload - so without
# this each reload would leave another subscriber behind, and N copies would all
# retab the same container N times. Same rule as picom/launch.sh.
_pidfile="${XDG_RUNTIME_DIR:-/tmp}/osr-min-tile.pid"
if [ -r "$_pidfile" ]; then
    _old=$(cat "$_pidfile" 2>/dev/null || true)
    if [ -n "${_old:-}" ] && [ "$_old" != "$$" ]; then
        # Children first. The subscriber runs in the pipeline's subshell, and
        # killing only the script leaves that subshell and its `i3-msg -t
        # subscribe` behind as orphans - a second reader of the same event
        # stream, retabbing containers nobody asked it to.
        pkill -P "$_old" 2>/dev/null || true
        kill "$_old" 2>/dev/null || true
    fi
fi
printf '%s\n' "$$" >"$_pidfile"
trap 'rm -f "$_pidfile"' EXIT HUP INT TERM

# The focused window's geometry, from X rather than from i3's tree: one small
# command, no JSON parser, and it is the size that was actually mapped.
_too_small() {
    _geom=$(xdotool getwindowfocus getwindowgeometry --shell 2>/dev/null) || return 1
    WIDTH=0
    HEIGHT=0
    eval "$_geom" 2>/dev/null || return 1
    # An underflowed size arrives as a huge unsigned number, so "too small" is
    # tested as "under the floor", NOT as "under the floor and above zero".
    [ "${WIDTH:-0}" -lt "$MIN_W" ] || [ "${HEIGHT:-0}" -lt "$MIN_H" ]
}

# `layout tabbed` applies to the focused container's PARENT split, which is the
# one that ran out of room. Focus returns to the new window, so the keystroke
# that opened it still lands where the user expects.
i3-msg -t subscribe -m '[ "window" ]' 2>/dev/null | while IFS= read -r _event; do
    case "$_event" in
        *'"change":"new"'*) ;;
        *) continue ;;
    esac
    # i3 maps and sizes the window a moment after announcing it.
    sleep 0.15
    _too_small || continue
    i3-msg -q 'focus parent, layout tabbed, focus child' 2>/dev/null || true
done
