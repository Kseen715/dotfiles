#!/bin/sh
# picom/launch.sh — dotfiles-owned (os-rice §5). Called by exec_always in the i3
# config, so it must be safe to run repeatedly: kill the old instance first or an
# i3 restart leaves two compositors fighting over the same windows.
#
# Why a launcher and not `exec picom --daemon`: picom.conf asks for the glx
# backend, and glx is not universally available. On an old GPU (Intel GMA, a
# blacklisted r300/i915) or a driverless VM, glx init fails and picom EXITS —
# and a dead compositor is not a cosmetic loss under i3. It is why rofi's
# rounded corners come out as opaque black squares (no ARGB visual, so
# `transparency: "real"` has nothing to be real against), why terminal
# transparency does nothing, and why shadows and fades are missing. The symptom
# never names the compositor, which is exactly what makes it expensive to find.
#
# So: try the configured (glx) path, and if picom is not still alive a moment
# later, come back up on xrender. xrender composites in software — no blur and
# no rounded corners (picom warns and carries on), but real transparency, real
# shadows and no tearing, which is the part that matters.
set -u

pkill -x picom 2>/dev/null || true
while pgrep -x picom >/dev/null 2>&1; do sleep 1; done

command -v picom >/dev/null 2>&1 || exit 0

_log="${XDG_RUNTIME_DIR:-/tmp}/picom.log"

# Start picom in the foreground-backgrounded (NOT --daemon: a daemonizing picom
# forks away immediately and the parent always exits 0, so there would be
# nothing left to test) and report whether it survived its own startup.
_try() {
    picom "$@" >"$_log" 2>&1 &
    _pid=$!
    sleep 2
    kill -0 "$_pid" 2>/dev/null
}

# 1. the configured path: glx + dual_kawase blur + rounded corners.
_try && exit 0

# 2. no usable GL. xrender cannot do dual_kawase, and picom treats an
#    unsupported blur method as a hard config error, so blur is turned off
#    explicitly rather than left to fail a second time.
_try --backend xrender --blur-method none && exit 0

# 3. neither backend came up. Leave the log where it can be read rather than
#    looping: the session still works, it is just uncomposited.
exit 1
