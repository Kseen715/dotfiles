#!/bin/sh
# polybar/launch.sh — dotfiles-owned (os-rice §5). Called by exec_always in the
# i3 config, so it must be safe to run repeatedly: kill the old instances first
# or an i3 restart stacks bars on top of each other.
#
# One bar per connected output, with MONITOR exported so `monitor = ${env:MONITOR:}`
# in config.ini resolves per instance. The systray can only be owned by one bar,
# so it is disabled on every output after the first (i3-sugg §12.9).
set -eu

pkill -x polybar 2>/dev/null || true
# Wait for the old bars to release the tray selection before claiming it again.
while pgrep -x polybar >/dev/null 2>&1; do sleep 1; done

_first=1
if command -v polybar >/dev/null 2>&1 && command -v xrandr >/dev/null 2>&1; then
    for _m in $(xrandr --query | awk '/ connected/ {print $1}'); do
        if [ "$_first" = 1 ]; then
            MONITOR=$_m polybar --reload main >/dev/null 2>&1 &
            _first=0
        else
            MONITOR=$_m polybar --reload secondary >/dev/null 2>&1 &
        fi
    done
fi

# No outputs found (headless, or xrandr missing): fall back to a single bar.
if [ "$_first" = 1 ]; then
    polybar --reload main >/dev/null 2>&1 &
fi

exit 0
