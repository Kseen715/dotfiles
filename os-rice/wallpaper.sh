#!/bin/sh
# wallpaper.sh — set or query the wallpaper of the current theme (§6a).
#
#   wallpaper.sh                 print the wallpaper in use
#   wallpaper.sh --list          print the library (theme images + ~/Pictures/Wallpapers)
#   wallpaper.sh <path>          make <path> the wallpaper of the current theme
#   wallpaper.sh --next          step to the next image in the library
#
# Separate from install.sh because it is not an install: no modules run, no
# layers are rewritten. It is the other half of what a picker needs - the theme
# says how things look, this says what is behind them.
#
# This file is a shim, the same shape install.sh has: the option loop, the
# current-theme resolution and the four actions are `osr wallpaper` in the
# harness core (lib/wallpaper_front.c), over the wallpaper family that had
# already moved into lib/config.c. It stays sh because it is the entry point
# people, pickers and hotkeys already type.
set -eu

# Delegates to ./osr for the same reason install.sh does: locating (and on a
# fresh checkout building) build/osr lives in exactly one file.
exec "$(cd -- "$(dirname -- "$0")" && pwd)/osr" wallpaper "$@"
