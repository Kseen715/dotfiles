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

# Stop any picom and wait until the X server has actually let go. This is not
# the same as waiting for the process to exit: the _NET_WM_CM_S0 selection is
# released when the server processes the client's DISCONNECT, which lands a
# moment later. Skip the settle and the next attempt dies with "Another
# composite manager is already running" — measured on a machine where the glx
# attempt fails, which is the only machine that ever reaches attempt two.
_stop() {
    pkill -x picom 2>/dev/null || true
    _n=0
    while pgrep -x picom >/dev/null 2>&1 && [ "$_n" -lt 15 ]; do
        sleep 1
        _n=$((_n + 1))
    done
    sleep 1
}

_stop

command -v picom >/dev/null 2>&1 || exit 0

_log="${XDG_RUNTIME_DIR:-/tmp}/picom.log"

# Start picom backgrounded (NOT --daemon: a daemonizing picom forks away
# immediately and the parent always exits 0, so there would be nothing left to
# test) and report whether it actually came up.
#
# "Still running" is NOT sufficient, and assuming it was cost a real bug. On
# this hardware glx sometimes dies outright ("Failed to get GLX context") and
# sometimes comes up HALF-INITIALISED, logging
#
#   [ glx_init ERROR ] Failed to enable vsync.
#
# and then staying alive while compositing incorrectly. A liveness probe calls
# that a success; what you get is a compositor that presents stale frames, so
# an override-redirect window drawn after its initial map - i3lock's clock and
# unlock ring, for instance - simply never reaches the screen. The lock screen
# looked like a plain blurred image with nowhere to type.
#
# So the backend must be alive AND quiet. The pattern list is narrow: a backend
# that could not initialise, not any warning picom happens to print.
_backend_broken='glx_init ERROR|Failed to enable vsync|Failed to get GLX|GLX_BAD|Failed to initialize backend|egl_init ERROR'

# Polled, not checked once. The failure is ASYNCHRONOUS: picom starts, maps its
# overlay, and only then fails to set up vsync - measured arriving a few seconds
# in, well after a single 2-second look had already declared success. That is
# how a broken compositor got adopted twice, and a broken compositor here does
# not look like a broken compositor: windows map, their backdrop is blurred, and
# the contents never arrive. rofi and alacritty both "froze" that way.
#
# Six seconds costs nothing on a healthy machine - picom is already compositing
# throughout the poll, and this script is backgrounded by exec_always.
_try() {
    picom "$@" >"$_log" 2>&1 &
    _pid=$!
    _n=0
    while [ "$_n" -lt 6 ]; do
        sleep 1
        _n=$((_n + 1))
        kill -0 "$_pid" 2>/dev/null || return 1
        if grep -qE "$_backend_broken" "$_log" 2>/dev/null; then
            kill "$_pid" 2>/dev/null || true
            return 1
        fi
    done
    return 0
}

# 1. the configured path: glx + dual_kawase blur + rounded corners.
_try && exit 0

# 2. no usable GL — on this hardware glx_init fails with GLX_BAD_FB_CONFIG and
#    picom aborts. xrender cannot do dual_kawase, and picom treats an
#    unsupported blur method as a hard config error, so blur is turned off
#    explicitly rather than left to fail a second time.
_stop
_try --backend xrender --blur-method none && exit 0

# 3. neither backend came up. Leave the log where it can be read rather than
#    looping: the session still works, it is just uncomposited.
exit 1
