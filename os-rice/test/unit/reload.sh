#!/bin/sh
# Proves lib/reload.sh (§6a): after the layers are on disk, the running programs
# are told to re-read them - and nothing else happens.
#
# _osr_try is replaced by a recorder, so this test decides WHICH reloader fires
# with WHICH arguments without ever executing one. That is not only for speed:
# osr_reload_gtk pokes gsettings, and a unit test must never mutate the dconf of
# whoever runs the suite.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

# --- a theme switch may never end the session --------------------------------
# The blast radius of this file is a logged-out user, so the dangerous verbs are
# asserted absent by inspection rather than left to reviewer discipline.
# Comment lines are stripped first: the file documents what it deliberately does
# NOT do, and matching that prose would make the check unfailable-by-rewording.
CODE="$T/reload-code.sh"
sed 's/^[[:space:]]*#.*$//' "$OSR_LIB/reload.sh" > "$CODE"
for danger in 'killall' 'pkill -9' 'kill -9' 'dispatch exit' 'systemctl restart' \
              'loginctl' 'pkill -x Hyprland' 'pkill -x i3'; do
    if grep -q -- "$danger" "$CODE"; then
        fail "reload.sh contains a session-ending verb: $danger"
    fi
done
ok "reload.sh contains no session-ending verb"

# waybar's SIGUSR1 toggles visibility and SIGUSR2 reloads: sending the wrong one
# hides the bar and looks exactly like a crash.
assert_contains "$OSR_LIB/reload.sh" 'USR2 -x waybar' "waybar is reloaded with USR2, not USR1"

# --- the recorder -------------------------------------------------------------
# Sourced fresh per scenario so OSR_RELOADED and the probe stubs never leak.
reload_run() {
    _rr_x11=$1; _rr_wl=$2; _rr_have=$3; _rr_running=$4
    (
        . "$OSR_LIB/reload.sh"
        LOG="$T/log"; : > "$LOG"
        # Record instead of execute - nothing in this test may run.
        _osr_try() { _l=$1; shift; printf '%s: %s\n' "$_l" "$*" >> "$LOG"; \
                     OSR_RELOADED="${OSR_RELOADED:+$OSR_RELOADED }$_l"; return 0; }
        # Probes answer from the scenario, not from the machine running the test.
        command() {
            [ "$1" = "-v" ] || { fail "unexpected command usage: $*"; return 1; }
            case " $_rr_have " in *" $2 "*) printf '%s\n' "/usr/bin/$2"; return 0 ;; esac
            return 1
        }
        _osr_running() { case " $_rr_running " in *" $1 "*) return 0 ;; esac; return 1; }
        DISPLAY=$_rr_x11; WAYLAND_DISPLAY=$_rr_wl
        OSR_HOME="$T/home"
        osr_reload_all >/dev/null 2>&1
        cat "$LOG" 2>/dev/null
    )
}

# --- a headless box reloads nothing -------------------------------------------
OUT=$(reload_run "" "" "i3-msg hyprctl dunstctl gsettings" "i3 Hyprland dunst")
assert_eq "" "$OUT" "no DISPLAY and no WAYLAND_DISPLAY reloads nothing"

# --- X11 session --------------------------------------------------------------
mkdir -p "$T/home"
printf '! xresources\n' > "$T/home/.Xresources"
OUT=$(reload_run ":0" "" "i3-msg xrdb dunstctl" "i3 polybar picom xsettingsd dunst")
printf '%s' "$OUT" | grep -q '^xrdb: ' && ok "X11: xrdb merges the new Xresources" \
    || fail "X11: xrdb should merge (got: $OUT)"
printf '%s' "$OUT" | grep -q '^i3: i3-msg -q restart' && ok "X11: i3 is restarted (colors + bar re-exec)" \
    || fail "X11: i3 should be restarted (got: $OUT)"
printf '%s' "$OUT" | grep -q '^picom: ' && ok "X11: picom re-reads its config" \
    || fail "X11: picom should reload (got: $OUT)"
printf '%s' "$OUT" | grep -q '^xsettingsd: ' && ok "X11: xsettingsd re-reads the GTK theme name" \
    || fail "X11: xsettingsd should reload"
printf '%s' "$OUT" | grep -q 'hyprland\|waybar' && fail "X11: a Wayland reloader fired" \
    || ok "X11: no Wayland reloader fires"

# xrdb must run BEFORE i3: i3 reads colors out of the X resource database, so
# the other order restarts the bar with the previous palette.
_xrdb_line=$(printf '%s\n' "$OUT" | grep -n '^xrdb:' | cut -d: -f1)
_i3_line=$(printf '%s\n' "$OUT" | grep -n '^i3:' | cut -d: -f1)
[ "$_xrdb_line" -lt "$_i3_line" ] && ok "xrdb runs before i3 re-reads its config" \
    || fail "xrdb must precede i3 (xrdb=$_xrdb_line i3=$_i3_line)"

# A program that is installed but not running is not signalled. xrdb is the one
# exception and stays: it merges into the X SERVER's resource database, not into
# a client, so it is meaningful with nothing else running - and it is what every
# client started afterwards will read.
OUT=$(reload_run ":0" "" "i3-msg xrdb" "")
assert_eq "xrdb: xrdb -merge $T/home/.Xresources" "$OUT" \
    "nothing running -> only the X resource database is refreshed"

# --- Wayland session ----------------------------------------------------------
OUT=$(reload_run "" "wayland-0" "hyprctl makoctl" "Hyprland waybar mako")
printf '%s' "$OUT" | grep -q '^hyprland: hyprctl reload' && ok "Wayland: hyprctl reload" \
    || fail "Wayland: hyprctl should reload (got: $OUT)"
printf '%s' "$OUT" | grep -q '^waybar: ' && ok "Wayland: waybar is reloaded" \
    || fail "Wayland: waybar should reload"
printf '%s' "$OUT" | grep -q 'i3\|polybar\|xrdb' && fail "Wayland: an X11 reloader fired" \
    || ok "Wayland: no X11 reloader fires"

# --- both (XWayland / a nested session) ---------------------------------------
OUT=$(reload_run ":0" "wayland-0" "hyprctl i3-msg xrdb" "Hyprland")
printf '%s' "$OUT" | grep -q '^hyprland: ' && ok "both set: the Wayland compositor still reloads" \
    || fail "both set: hyprland should reload"
printf '%s' "$OUT" | grep -q '^i3: ' && fail "both set: i3 reloaded while not running" \
    || ok "both set: i3 is not reloaded (not running)"

# --- _osr_try really does swallow failure -------------------------------------
(
    . "$OSR_LIB/reload.sh"
    OSR_RELOADED=""
    _osr_try "boom" false && ok "_osr_try returns 0 when the reloader fails" \
        || fail "_osr_try must never propagate a failure"
    assert_eq "" "$OSR_RELOADED" "a failed reloader is not reported as reloaded"
    # ...and osr_reload_all survives a reloader that dies mid-way.
    set -e
    DISPLAY=""; WAYLAND_DISPLAY=""
    osr_reload_all >/dev/null 2>&1 && ok "osr_reload_all returns 0 on a headless box" \
        || fail "osr_reload_all must return 0"
    finish
) || exit 1

finish
