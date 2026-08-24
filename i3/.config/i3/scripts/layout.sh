#!/bin/sh
# i3/scripts/layout.sh — dotfiles-owned (os-rice §5). Backs the layout bindings
# ($mod+w tabbed, $mod+s toggle split) and the 4-finger swipe.
#
# Why this is not just `i3-msg layout tabbed`:
#
# autotiling runs on every focus event and issues `split v` / `split h` at the
# focused window. i3 answers a split on a leaf that has siblings by WRAPPING it
# in a brand-new split container holding only that leaf. So the tree under a
# plain two-window split is really:
#
#   workspace
#   └── splith                <- the container you can see, and mean
#       ├── leaf A
#       └── splitv            <- autotiling's wrapper, one child, invisible
#           └── leaf B        <- focused
#
# `layout tabbed` acts on the focused container's parent — the wrapper. It
# becomes a tabbed container with exactly one tab, which looks like nothing
# happened, and then every new window opens as a tab INSIDE it. That is the bug:
# not a dead keybinding, a binding aimed one level too low.
#
# So: walk up past single-child wrappers to the nearest ancestor that actually
# holds more than one thing, and address that container by con_id. With no
# wrapper in the way (autotiling off, or a lone window) the walk stops
# immediately and this behaves exactly like the plain command.
#
# python3 rather than jq: autotiling is a Python program, so python3 is already
# a hard dependency of this session and jq is not.
set -eu

[ $# -ge 1 ] || { echo "usage: layout.sh <tabbed|stacking|toggle-split|splith|splitv>" >&2; exit 2; }

case $1 in
    toggle-split) _cmd="layout toggle split" ;;
    *)            _cmd="layout $1" ;;
esac

_id=$(i3-msg -t get_tree | python3 -c '
import json, sys

tree = json.load(sys.stdin)

# Parent links, and the focused leaf. get_tree gives no upward pointers.
parent = {}
focused = None
stack = [tree]
while stack:
    node = stack.pop()
    if node.get("focused"):
        focused = node
    for kid in node.get("nodes", []) + node.get("floating_nodes", []):
        parent[kid["id"]] = node
        stack.append(kid)

if focused is None:
    sys.exit(1)

# Start at the focused container and climb. Stop at the first ancestor with more
# than one child (the real container) or at the workspace, which is as far up as
# a layout command is allowed to reach anyway.
node = focused
while True:
    up = parent.get(node["id"])
    if up is None or up["type"] in ("workspace", "output", "root"):
        # Nothing above but the workspace: address it directly, so a single
        # window on an empty workspace still becomes tabbed.
        print(up["id"] if up is not None and up["type"] == "workspace" else node["id"])
        break
    if len(up.get("nodes", [])) > 1:
        print(up["id"])
        break
    node = up
') || exit 0

[ -n "$_id" ] && exec i3-msg "[con_id=$_id] $_cmd" >/dev/null
