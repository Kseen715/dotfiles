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
#
# The mechanism is `focus parent`, not `[con_id=N] layout`. Addressing a
# container by id looks like the direct route and is not: a split container has
# no window, i3's criteria do not reliably match it, and the command silently
# lands on the workspace instead - measured, it wraps the tree in yet another
# level rather than retitling the container you named. `layout` on a focused
# LEAF, by contrast, is well defined: it applies to that leaf's parent. So climb
# exactly as many levels as there are single-child wrappers, apply, and put focus
# back on the window that had it.
set -eu

[ $# -ge 1 ] || { echo "usage: layout.sh <tabbed|stacking|toggle-split|splith|splitv>" >&2; exit 2; }

case $1 in
    toggle-split) _cmd="layout toggle split" ;;
    *)            _cmd="layout $1" ;;
esac

# "<levels to climb> <con_id of the focused window>"
_plan=$(i3-msg -t get_tree | python3 -c '
import json, sys

tree = json.load(sys.stdin)

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

# Count the single-child wrappers between the focused window and the container
# that actually holds more than one thing. Zero of them (autotiling off, or a
# lone window) means this behaves exactly like the plain layout command.
levels = 0
node = focused
while True:
    up = parent.get(node["id"])
    if up is None or up["type"] in ("workspace", "output", "root"):
        break
    if len(up.get("nodes", [])) > 1:
        break
    levels += 1
    node = up

print(levels, focused["id"])
') || exit 0

[ -n "$_plan" ] || exit 0
_levels=${_plan%% *}
_leaf=${_plan##* }

# One i3-msg, so the whole thing is atomic from autotiling's point of view: it
# reacts to focus events, and half-applied intermediate states are exactly what
# put the stray wrappers there in the first place.
_chain=""
_i=0
while [ "$_i" -lt "$_levels" ]; do
    _chain="$_chain focus parent;"
    _i=$((_i + 1))
done

exec i3-msg "$_chain $_cmd; [con_id=$_leaf] focus" >/dev/null
