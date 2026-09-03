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
# The supervisor of the PREVIOUS run (see the bottom of this file). exec_always
# runs this script again on every i3 reload, and without this each reload would
# leave another supervisor behind, all of them racing to restart picom.
_sup_pid="${XDG_RUNTIME_DIR:-/tmp}/osr-picom-supervisor.pid"

_stop() {
    if [ -r "$_sup_pid" ]; then
        _old=$(cat "$_sup_pid" 2>/dev/null || true)
        [ -n "${_old:-}" ] && kill "$_old" 2>/dev/null || true
        rm -f "$_sup_pid"
    fi
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
# Twenty seconds, not six. The latency of the failure is the whole problem: on
# the Ironlake/Optimus box this was measured on, X comes up with AccelMethod
# none, GLX falls back to llvmpipe, and picom logs
#
#   [ glx_init ERROR ] Failed to enable vsync.
#
# THIRTEEN seconds after start - past a six-second window, so the poll declared
# the glx attempt healthy and the xrender fallback never ran. What you get is a
# software-GL compositor with no vsync: every full-screen repaint (a wallpaper
# change is exactly one) tears and flickers.
#
# The wait costs nothing on a healthy machine - picom is already compositing
# throughout the poll, and this script is backgrounded by exec_always.
_try() {
    # This attempt's own file, so the pattern below is matched against THIS
    # picom's output and not against an error some earlier attempt (or an
    # earlier crash, now that the log survives restarts) left behind.
    _cur="${XDG_RUNTIME_DIR:-/tmp}/picom.current"
    : >"$_cur"
    printf '=== %s: picom %s ===\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*" >>"$_log"
    picom "$@" >"$_cur" 2>&1 &
    _pid=$!
    _n=0
    while [ "$_n" -lt 20 ]; do
        sleep 1
        _n=$((_n + 1))
        if ! kill -0 "$_pid" 2>/dev/null; then
            cat "$_cur" >>"$_log"
            return 1
        fi
        if grep -qE "$_backend_broken" "$_cur" 2>/dev/null; then
            kill "$_pid" 2>/dev/null || true
            cat "$_cur" >>"$_log"
            return 1
        fi
    done
    cat "$_cur" >>"$_log"
    return 0
}

# _supervise -- keep the compositor alive for the life of the session.
#
# picom is not a program that only fails at startup. Measured on this box, hours
# into a session:
#
#   [ x_create_picture_with_pictfmt_and_pixmap ERROR ] failed to create picture
#   (X error 8 MATCH request 139 minor 4 serial 85015)
#
# - a BadMatch on a RENDER CreatePicture, and picom is gone. Nothing restarted
# it, because exec_always runs this script once per i3 start or reload, so the
# session simply lost its compositor: terminal transparency off, shadows gone,
# rofi opaque. From the desk that reads as "transparency turns itself off
# sometimes", which names neither picom nor a crash and is why it went
# unexplained.
#
# So the launcher does not exit once picom is up - it waits on it and starts it
# again, with the backend that was already proven to work in the tiers below.
#
# The cap is the other half. A compositor that cannot stay up for five seconds
# is not going to be fixed by a sixth restart, and a hot restart loop on a
# machine this slow is worse than no compositor: five failures inside a minute
# stops the loop and SAYS so, rather than spinning silently until the session
# ends.
_supervise() {
    printf '%s\n' "$$" >"$_sup_pid"
    _fails=0
    _window=$(date +%s)
    while :; do
        wait "$_pid" 2>/dev/null || true
        _now=$(date +%s)
        [ $((_now - _window)) -gt 60 ] && { _window=$_now; _fails=0; }
        _fails=$((_fails + 1))
        if [ "$_fails" -gt 5 ]; then
            printf '=== %s: giving up after %s restarts in under a minute ===\n' \
                "$(date '+%Y-%m-%d %H:%M:%S')" "$_fails" >>"$_log"
            command -v notify-send >/dev/null 2>&1 &&
                notify-send -u critical "Compositor keeps crashing" \
                    "picom exited 5 times in a minute; not restarting it again. See $_log."
            rm -f "$_sup_pid"
            return 1
        fi
        printf '=== %s: picom exited - restarting (%s in this minute) ===\n' \
            "$(date '+%Y-%m-%d %H:%M:%S')" "$_fails" >>"$_log"
        sleep 2
        picom "$@" >>"$_log" 2>&1 &
        _pid=$!
    done
}

# 1. the configured path: glx + dual_kawase blur + rounded corners.
_try && _supervise && exit 0

# 2. no usable GL — on this hardware glx_init fails with GLX_BAD_FB_CONFIG and
#    picom aborts. xrender cannot do dual_kawase, and picom treats an
#    unsupported blur method as a hard config error, so blur is turned off
#    explicitly rather than left to fail a second time.
#
#    --no-use-damage is the third flag and it is not a tuning: on this backend
#    picom keeps the OLD window pixmap after a window is resized, and a tiling
#    WM resizes every window on screen each time one opens. What you get is a
#    window that stops updating and shows the wallpaper through where its
#    contents should be - reported as "the session froze with three terminals
#    open", which is what it looks like from the desk.
#
#    Measured, as the mean pixel inside one terminal: (37,36,38) composited
#    correctly, (76,60,67) once another window opened and the pixmap went
#    stale, and (37,36,38) again after the resize WITH this flag.
#    --xrender-sync-fence was tried on the same box and does not help; damage
#    tracking is what has to go. The cost is a full-screen repaint per frame,
#    which on the 1366x768 panel this class of machine has measured at under
#    1% CPU - and it is only ever paid here, on the fallback path, by a box
#    that already has no GPU acceleration at all.
_stop
_try --backend xrender --blur-method none --no-use-damage &&
    _supervise --backend xrender --blur-method none --no-use-damage && exit 0

# 3. neither backend came up. Leave the log where it can be read rather than
#    looping: the session still works, it is just uncomposited.
exit 1
