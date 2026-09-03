#!/bin/sh
# i3/scripts/term.sh — dotfiles-owned (os-rice §5). Installed by modules/i3.sh
# as /usr/local/bin/osr-term (on PATH, because rofi's `terminal:` is tokenized
# rather than shell-expanded and cannot spell a ~/ path). i3's $term, rofi and
# the xfce4 helpers.rc all name it, so every "open a terminal" path in the
# session goes through one policy instead of three.
#
# Why a launcher and not just `alacritty`: every terminal this rice would pick
# is GPU-accelerated. Ghostty wants a real OpenGL 3.3 context and dies on an old
# laptop GPU with "No EGL configuration available"; alacritty's bar is far lower
# but it is still a GL program. When one of them refuses to start, $mod+Return
# does NOTHING AT ALL, silently — the worst possible failure mode for the one
# binding you need in order to debug the machine (helpers.c §3 makes the same
# argument about shipping xterm).
#
# So this degrades, and every step still gives you a terminal:
#   1. $OSR_TERMINAL             — an explicit per-machine override, if set
#   2. the rice's terminal       — alacritty, which asks only for OpenGL ES 2.0
#                                  rather than the 3.3 ghostty wants
#   3. the same one on llvmpipe  — software GL. Slower, but the same terminal,
#                                  same config, same keybinds; a tired GPU
#                                  costs you frames, not your setup. Measured:
#                                  on a GT 420M / Ironlake laptop where ghostty
#                                  1.1.3 cannot get an EGL config at all, this
#                                  step is what actually produces a terminal.
#   4. whatever else is present  — ending at xterm, which the helpers module
#                                  installs precisely so this list can never
#                                  come up empty.
#
# The order below IS the rice's terminal preference. i3-rosemary ships alacritty
# (rice.list), so alacritty leads; ghostty stays in the list because a rice that
# installs it should still get it when alacritty is absent.
#
# Arguments are passed straight through, so `osr-term -e htop` works: -e is
# spelled the same by alacritty, ghostty, wezterm, foot, kitty and xterm.
set -u

# Start the candidate and report whether it actually came up.
#
# "Still running" is NOT the test, and assuming it was is how this script got
# written wrong the first time. Measured on Ghostty 1.1.3 with no usable EGL
# config: the process does not exit. It stays alive, having logged
#
#   error(gtk_surface): surface failed to realize: No EGL configuration available
#
# and shows a window that never renders a terminal. A liveness probe calls that
# a success and hands you the broken thing, which is precisely the failure the
# fallback exists to route around.
#
# So: dead is a failure, and alive-but-having-logged-a-surface-failure is also a
# failure. The pattern list is deliberately narrow - a renderer that cannot get
# a context - so an ordinary warning on stderr never costs you your terminal.
# The pattern list is deliberately narrow - a renderer that cannot get a context
# - so an ordinary warning on stderr never costs you your terminal. "Unable to
# create a GL context" is in it because on Ghostty 1.1.3 that is the FIRST thing
# logged; "surface failed to realize" follows it a second or two later, which is
# why this is polled rather than checked once at a fixed instant.
_failed_re='surface failed to realize|No EGL configuration|Unable to create a GL context|Failed to initialize|GLX_BAD|Failed to get GLX|EGL_BAD'

# TWO files, and the split is the whole correctness argument.
#
# $_out is THIS INVOCATION's, named by pid. Nothing else writes it, so the
# failure test below reads the output of the terminal this script started and
# of nothing else. It used to be one fixed path shared by every invocation, and
# that was a real race, not a tidiness point: hold $mod+Return and i3 runs this
# script a dozen times in the same second (measured: eleven in one second on
# the box this was written on). Each one truncated the shared file and then
# grepped it, so an instance would find ANOTHER instance's "No EGL
# configuration" in there, conclude that its own perfectly healthy alacritty
# had failed, kill it, and fall through to the next terminal in the list. That
# is the "sometimes $mod+Return opens ghostty instead" report, in full.
#
# $_log is the shared history, APPEND-only - never truncated mid-run, so
# concurrent instances interleave sections instead of erasing each other. It
# used to be truncated per attempt, which destroyed the one thing worth
# keeping: by the time you noticed the wrong terminal, the log held the output
# of the one that worked and nothing about the one that did not.
_log="${XDG_RUNTIME_DIR:-/tmp}/osr-term.log"
_out="${XDG_RUNTIME_DIR:-/tmp}/osr-term.$$.out"
# Size guard before anything is written, and only from an instance that finds
# the file already oversized - never while this run is appending to it.
[ -f "$_log" ] && [ "$(wc -c <"$_log")" -gt 262144 ] && : >"$_log"
trap 'rm -f "$_out"' EXIT HUP INT TERM

_try() {
    _cur=$_out
    : >"$_cur"
    printf '=== %s [pid %s]: %s ===\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$$" "$*" >>"$_log"
    "$@" >"$_cur" 2>&1 &
    _pid=$!
    # Poll rather than sleep-then-look: the failure arrives asynchronously, some
    # of it only once the window tries to realize. Five seconds is long enough
    # for that on a slow disk and short enough that $mod+Return still feels
    # instant on the machine where the first candidate simply works.
    _n=0
    while [ "$_n" -lt 5 ]; do
        sleep 1
        _n=$((_n + 1))
        if ! kill -0 "$_pid" 2>/dev/null; then
            printf '(exited within %ss)\n' "$_n" >>"$_cur"
            cat "$_cur" >>"$_log"
            return 1
        fi
        if grep -qE "$_failed_re" "$_cur" 2>/dev/null; then
            kill "$_pid" 2>/dev/null || true
            printf '(killed: matched the renderer-failure pattern)\n' >>"$_cur"
            cat "$_cur" >>"$_log"
            return 1
        fi
    done
    cat "$_cur" >>"$_log"
    return 0
}

# Which candidate is being tried, and why the previous one was passed over, is
# announced ONCE per fallback. Silence here is what turns "alacritty is the
# rice's terminal" into "sometimes $mod+Return opens a different terminal and I
# have no idea why" - the chain is doing its job in that moment, and the only
# thing wrong is that it does it in secret.
_fell_back=""
for _t in ${OSR_TERMINAL:-} alacritty ghostty wezterm foot kitty xterm; do
    command -v "$_t" >/dev/null 2>&1 || continue
    if [ -n "$_fell_back" ]; then
        command -v notify-send >/dev/null 2>&1 &&
            notify-send -u normal "$_fell_back did not start" \
                "Falling back to $_t. Reason is in ${XDG_RUNTIME_DIR:-/tmp}/osr-term.log."
    fi
    _try "$_t" "$@" && exit 0
    # It is installed and it did not come up. On this class of machine that is
    # almost always the GL context, so give the SAME terminal a second chance on
    # llvmpipe before moving on to a different one. GSK_RENDERER=cairo covers a
    # GTK4 terminal whose window chrome goes through its own, equally broken,
    # GL path.
    _try env LIBGL_ALWAYS_SOFTWARE=1 GSK_RENDERER=cairo "$_t" "$@" && exit 0
    _fell_back=$_t
done

# Nothing at all came up. Say so where it can actually be seen — an i3 exec that
# fails writes to a log nobody has open.
command -v notify-send >/dev/null 2>&1 &&
    notify-send -u critical "No terminal could start" \
        "alacritty, and every fallback, failed to open. See ~/.xsession-errors."
exit 1
