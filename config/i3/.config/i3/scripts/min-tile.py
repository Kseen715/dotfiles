#!/usr/bin/env python3
"""i3/scripts/min-tile.py -- dotfiles-owned (os-rice §5). A floor under tile size.

i3 has no minimum tile size and will keep halving forever. Past a certain count
that is not merely unreadable, it is degenerate: measured on a 1366x768 panel
with fourteen windows in one column, i3 handed out rects of 11x9, 7x4 and then
sizes that had underflowed entirely -

    7x4294967289        (that is -7, as an unsigned 32-bit value)

- and a window with no area is a window whose pixmap cannot be turned into a
RENDER picture. picom takes the BadMatch and dies:

    x_create_picture_with_pictfmt_and_pixmap ERROR ] failed to create picture
    (X error 8 MATCH request 139 minor 4 serial 28509)

so the whole session loses its compositor because one more terminal was opened.

Below the floor the container becomes TABBED instead of splitting again: every
window stays on the workspace, full size, one keystroke apart. Refusing the
window is not available (it exists by the time anything can react to it) and
floating it would drop it over the workspace it was meant to join.

WHY THIS READS THE TREE, AND WHY THAT IS THE WHOLE POINT

The first version of this was shell, and it tabbed the parent whenever the new
window was too small -- without checking what the parent already was. Tabbing a
container that is ALREADY tabbed nests a tab set inside a tab set, and i3 then
keeps one window mapped per nesting level: two translucent terminals in the
same rect, blended into each other. Reported as "all tabs of the pane render
simultaneously, I can see both".

"Is the parent already tabbed" is not answerable from X - it is a property of
i3's layout tree - so the check needs the tree, and that is what makes this
Python rather than the sh the other i3 scripts are. python3 is already a
dependency of this rice through autotiling, which the same i3 config execs.
"""
import json
import os
import subprocess
import sys

MIN_W = int(os.environ.get("OSR_MIN_TILE_WIDTH", "240"))
MIN_H = int(os.environ.get("OSR_MIN_TILE_HEIGHT", "120"))

# Layouts that already stack their children. Tabbing one of these is what
# created the nesting bug, and a stacked container has the same property that
# makes tabbing worth doing: every child gets the container's full size.
STACKED = ("tabbed", "stacked")


def i3(*args):
    """i3-msg, or None when it fails -- a WM that is restarting is not an error."""
    try:
        out = subprocess.run(("i3-msg",) + args, capture_output=True, timeout=5)
    except (OSError, subprocess.SubprocessError):
        return None
    return out.stdout if out.returncode == 0 else None


def focused_with_parent(node, parent=None):
    """The focused window node and the container holding it."""
    if node.get("focused") and node.get("window"):
        return node, parent
    for key in ("nodes", "floating_nodes"):
        for child in node.get(key, ()):
            found = focused_with_parent(child, node)
            if found:
                return found
    return None


def too_small(rect):
    # An underflowed size arrives as a huge unsigned number, so "too small" is
    # tested as "under the floor", never as "under the floor and above zero".
    return rect.get("width", 0) < MIN_W or rect.get("height", 0) < MIN_H


def guard():
    raw = i3("-t", "get_tree")
    if raw is None:
        return
    try:
        found = focused_with_parent(json.loads(raw))
    except (ValueError, KeyError):
        return
    if not found:
        return
    window, parent = found
    if not too_small(window.get("rect", {})):
        return
    if parent is None or parent.get("layout") in STACKED:
        # Already stacked: the windows are full size and the floor is met.
        # Tabbing again would nest one tab set inside another.
        return
    i3("-q", "focus parent, layout tabbed, focus child")


def main():
    try:
        events = subprocess.Popen(
            ("i3-msg", "-t", "subscribe", "-m", '[ "window" ]'),
            stdout=subprocess.PIPE, text=True)
    except OSError:
        return 0
    for line in events.stdout:
        if '"change":"new"' not in line:
            continue
        guard()
    return 0


if __name__ == "__main__":
    sys.exit(main())
