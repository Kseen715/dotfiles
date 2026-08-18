#!/bin/sh
# Proves lib/ui.sh (now a shim over `osr ui` in the harness core) still prints the
# exact bytes the pure-sh implementation printed. The old implementation is
# frozen verbatim at test/ref/ui_sh_ref.sh and is the reference here: every
# check below runs the same operation through both and compares hex dumps, so
# a single changed escape byte fails the test.
#
# Covered: the color/width variables, step_prefix, _step_paint over a matrix of
# log fixtures x already-painted rows x palette x width, _step_done, _spin
# (both the "process already gone" case, which is byte-exact end to end, and a
# live process, where frame 1 is compared exactly), run_step's [ok]/[!!] result
# line, its non-TTY branch, and the on-failure log dump.
#
# Not covered, because it is not deterministic in either implementation: how
# MANY spinner frames a live step paints - that is wall-clock divided by the
# 0.2s repaint interval. Frame 1 is byte-exact and every later frame is the
# same block again with the cursor-up count in front of it.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
OSR_REF="$OSR_ROOT/test/ref/ui_sh_ref.sh"; export OSR_REF
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

# The environment both sides run in must be identical AND explicit: TERM/
# COLUMNS leak into `tput cols` on the sh side and into the ioctl on the C
# side, and OSR_* would be inherited from this test's own shell.
BASE_ENV='TERM=dumb OSR_STEP_N= OSR_STEP_TOTAL= OSR_TAIL_LINES= COLUMNS= NO_COLOR='

hex() { od -An -tx1 | tr -d ' \n'; }

# same <label> <ref-bytes> <c-bytes>
same() {
    if [ "$2" = "$3" ]; then
        ok "$1"
    else
        fail "$1"
        printf '    ref: %s\n    c  : %s\n' "$2" "$3" >&2
    fi
}

# --- fixtures ----------------------------------------------------------------
# One per property of the sh pipeline `tail -n N | tr -d '\r' | sed <CSI> |
# cut -c 1-COLS` inside a `$(...)`.
FX="$TMP/fx"; mkdir -p "$FX"
printf 'a\nb\nc\nd\ne\nf\ng\n'                        >"$FX/plain.log"
printf 'one\r\ntwo\r\n'                               >"$FX/crlf.log"
printf '\033[32mgreen\033[0m\n\033[?25lhide\033[K\nplain\n'          >"$FX/ansi.log"
printf 'esc \033[3 unterminated\nlone \033 esc\n'      >"$FX/badesc.log"
printf 'no trailing newline'                          >"$FX/nonl.log"
printf 'keep\n\n\n'                                   >"$FX/trailblank.log"
printf '\n\n\n'                                       >"$FX/blank.log"
:                                                     >"$FX/empty.log"
awk 'BEGIN { s=""; while (length(s) < 300) s = s "x"; print s }' >"$FX/long.log"
awk 'BEGIN { for (i = 1; i <= 5000; i++) print i }'   >"$FX/big.log"
FIXTURES='plain crlf ansi badesc nonl trailblank blank empty long big missing'

# --- 1. the sourced variables ------------------------------------------------
# ui.sh's job on the way in: six palette vars (LITERAL '\033[..' - lib/log.sh
# expands them with %b) plus OSR_COLS. Piped output means no color, both sides.
for _nc in 1 ''; do
    _snip='printf "%s|%s|%s|%s|%s|%s|%s\n" "$OSR_RED" "$OSR_GREEN" "$OSR_YELLOW" "$OSR_CYAN" "$OSR_DIM" "$OSR_NC" "$OSR_COLS"'
    # shellcheck disable=SC2086  # deliberate word split into env assignments
    _r=$(env $BASE_ENV NO_COLOR="$_nc" sh -c '. "$OSR_REF"; '"$_snip" | hex)
    # shellcheck disable=SC2086
    _c=$(env $BASE_ENV NO_COLOR="$_nc" sh -c '. "$OSR_LIB/ui.sh"; '"$_snip" | hex)
    same "vars: palette + OSR_COLS (NO_COLOR='$_nc', piped)" "$_r" "$_c"
done

# --- 2. step_prefix ----------------------------------------------------------
for _pair in '0 0' '1 12' '3 12' '9 9' '10 100'; do
    # shellcheck disable=SC2086  # deliberate split into "n total"
    set -- $_pair
    # shellcheck disable=SC2086
    _r=$(env $BASE_ENV OSR_STEP_N="$1" OSR_STEP_TOTAL="$2" sh -c '. "$OSR_REF"; step_prefix' | hex)
    # shellcheck disable=SC2086
    _c=$(env $BASE_ENV OSR_STEP_N="$1" OSR_STEP_TOTAL="$2" sh -c '. "$OSR_LIB/ui.sh"; step_prefix' | hex)
    same "step_prefix: n=$1 total=$2" "$_r" "$_c"
done

# --- 3. _step_paint over the matrix ------------------------------------------
# Descriptions include backslashes on purpose: the sh version printed the
# status line with `%b`, so `\n` and `\c` in a description are expanded, and a
# port that used `%s` would silently differ here.
#
# The palette is passed as arguments, not in the environment: sourcing the
# reference DECIDES the palette (that `if [ -t 1 ]` block is the first thing it
# runs), so an inherited OSR_DIM would be overwritten there and compared
# against nothing. The binary reads the same two values from its environment,
# which is where the shim's exported palette reaches it.
_paint_diffs=0
_paint_cases=0
for _pal in plain color; do
    case "$_pal" in
        plain) _dim='' _nc='' ;;
        color) _dim='\033[2m' _nc='\033[0m' ;;
    esac
    for _env in '' 'OSR_TAIL_LINES=0' 'OSR_TAIL_LINES=2' 'COLUMNS=25' 'COLUMNS=5'; do
        for _fx in $FIXTURES; do
            for _p in 0 4; do
                for _desc in '| building thing' 'back\slash and \n' 'percent %s %d' 'stop\chere'; do
                    _paint_cases=$((_paint_cases + 1))
                    # shellcheck disable=SC2086
                    _r=$(env $BASE_ENV $_env sh -c \
                        '. "$OSR_REF"; OSR_DIM=$1; OSR_NC=$2
                         _OSR_PAINTED=$3; _OSR_STEP_LOG=$4; _step_paint "$5"' \
                        _ "$_dim" "$_nc" "$_p" "$FX/$_fx.log" "$_desc" | hex)
                    # shellcheck disable=SC2086
                    _c=$(env $BASE_ENV $_env OSR_DIM="$_dim" OSR_NC="$_nc" \
                        "$OSR_BIN" ui paint "$_p" "$FX/$_fx.log" "$_desc" | hex)
                    if [ "$_r" != "$_c" ]; then
                        _paint_diffs=$((_paint_diffs + 1))
                        printf '    paint diff: pal=%s env=[%s] fx=%s painted=%s desc=[%s]\n' \
                            "$_pal" "$_env" "$_fx" "$_p" "$_desc" >&2
                    fi
                done
            done
        done
    done
done
assert_eq 0 "$_paint_diffs" "_step_paint: $_paint_cases cases byte-identical"

# --- 4. _step_done -----------------------------------------------------------
for _p in 0 1 5; do
    # shellcheck disable=SC2086
    _r=$(env $BASE_ENV NO_COLOR=1 sh -c '. "$OSR_REF"; _OSR_PAINTED=$1; _step_done "$2"' \
        _ "$_p" '[ok] a step' | hex)
    # shellcheck disable=SC2086
    _c=$(env $BASE_ENV NO_COLOR=1 "$OSR_BIN" ui "done" "$_p" '[ok] a step' | hex)
    same "_step_done: erase $_p painted rows" "$_r" "$_c"
done

# --- 5. the result line run_step ends with -----------------------------------
# sh built it with printf and handed it to _step_done; C composes the same
# string from the same palette vars, so the whole tail of run_step is compared
# here, colors on and off. The reference snippet is run_step's own two lines,
# copied.
for _status in ok fail; do
    for _pal in plain color; do
        case "$_pal" in
            plain) _green='' _red='' _nc='' ;;
            color) _green='\033[0;32m' _red='\033[0;31m' _nc='\033[0m' ;;
        esac
        printf '3\n' >"$TMP/paint.state"
        # shellcheck disable=SC2086
        _r=$(env $BASE_ENV sh -c \
            '. "$OSR_REF"; OSR_GREEN=$1; OSR_RED=$2; OSR_NC=$3; _OSR_PAINTED=3
             if [ "$4" = ok ]; then
                 _step_done "$(printf "%b[ok]%b %s" "$OSR_GREEN" "$OSR_NC" "$5")"
             else
                 _step_done "$(printf "%b[!!]%b %s" "$OSR_RED" "$OSR_NC" "$5")"
             fi' \
            _ "$_green" "$_red" "$_nc" "$_status" 'Installing polybar' | hex)
        # shellcheck disable=SC2086
        _c=$(env $BASE_ENV OSR_GREEN="$_green" OSR_RED="$_red" OSR_NC="$_nc" \
            "$OSR_BIN" ui result "$TMP/paint.state" "$_status" 'Installing polybar' | hex)
        same "run_step result line: $_status, palette $_pal" "$_r" "$_c"
    done
done
if [ -f "$TMP/paint.state" ]; then
    fail "result: state file not cleaned up"
else
    ok "result: state file consumed"
fi

# --- 6. _spin ----------------------------------------------------------------
# A pid that is already gone paints nothing at all - deterministic, so the
# whole call is compared byte for byte. 999999 is above the default pid_max on
# every target and is verified free first.
_dead=999999
while kill -0 "$_dead" 2>/dev/null; do _dead=$((_dead - 1)); done
# shellcheck disable=SC2086
_r=$(env $BASE_ENV NO_COLOR=1 sh -c '. "$OSR_REF"; _OSR_STEP_LOG=$2; _spin "$1" "a step"' \
    _ "$_dead" "$FX/plain.log" | hex)
# shellcheck disable=SC2086
_c=$(env $BASE_ENV NO_COLOR=1 "$OSR_BIN" ui spin "$_dead" "a step" "$FX/plain.log" "$TMP/spin.state" | hex)
same "_spin: dead pid paints nothing" "$_r" "$_c"
assert_eq "0" "$(cat "$TMP/spin.state")" "spin: records 0 painted rows"

# A live process: frame 1 is fully determined (no cursor-up, frame char '|',
# the fixture's 5 tail lines + the status line), so compare those 6 lines.
#
# Neither side may `exec` the painter: the watched process must stay a child of
# a shell that reaps it, which is exactly the arrangement run_step sets up. A
# painter that inherits the child instead waits on a zombie forever - true of
# the sh `kill -0` loop as much as of the C one.
# shellcheck disable=SC2086
env $BASE_ENV NO_COLOR=1 sh -c \
    'sleep 1 & . "$OSR_REF"; _OSR_STEP_LOG=$1; _spin $! "a step"' \
    _ "$FX/plain.log" >"$TMP/spin.ref" 2>/dev/null
# shellcheck disable=SC2086
env $BASE_ENV NO_COLOR=1 sh -c \
    'sleep 1 & "$OSR_BIN" ui spin $! "a step" "$1" "$2"' \
    _ "$FX/plain.log" "$TMP/spin.state" >"$TMP/spin.c" 2>/dev/null
_r=$(head -n 6 "$TMP/spin.ref" | hex)
_c=$(head -n 6 "$TMP/spin.c" | hex)
same "_spin: live pid, frame 1 byte-identical" "$_r" "$_c"
if [ "$(wc -l <"$TMP/spin.c")" -ge 12 ]; then
    ok "_spin: live pid repaints (>= 2 frames in 1s)"
else
    fail "_spin: live pid repainted only $(wc -l <"$TMP/spin.c") lines"
fi

# --- 7. run_step, non-TTY branch ---------------------------------------------
# Piped output takes the plain path in both: one info line, then the command's
# own output streamed straight through. stdout and stderr both compared.
_rs='. "$OSR_REF"; . "$OSR_LIB/log.sh"; run_step "Installing thing" sh -c "echo out; echo err >&2"'
# shellcheck disable=SC2086
_r=$(env $BASE_ENV NO_COLOR=1 sh -c "$_rs" 2>&1 | hex)
# shellcheck disable=SC2086
_c=$(env $BASE_ENV NO_COLOR=1 sh -c '. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; run_step "Installing thing" sh -c "echo out; echo err >&2"' 2>&1 | hex)
same "run_step: non-TTY branch streams the same bytes" "$_r" "$_c"

# A failing step is fatal in both, with the same message and exit status.
_fail_sh='run_step "Installing thing" sh -c "exit 3"'
# shellcheck disable=SC2086
_r=$(env $BASE_ENV NO_COLOR=1 sh -c '. "$OSR_REF"; . "$OSR_LIB/log.sh"; '"$_fail_sh" 2>&1 | hex); _rrc=$?
# shellcheck disable=SC2086
_c=$(env $BASE_ENV NO_COLOR=1 sh -c '. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; '"$_fail_sh" 2>&1 | hex); _crc=$?
same "run_step: non-TTY failure is fatal, same bytes" "$_r" "$_c"
assert_eq "$_rrc" "$_crc" "run_step: non-TTY failure, same exit status"

# --- 8. the on-failure log dump ----------------------------------------------
# sh ran `tail -n 20 <log> >&2`; C reads it itself. Raw bytes, no filtering -
# a failed step must show exactly what the command printed.
for _fx in plain big nonl empty missing; do
    _r=$(tail -n 20 "$FX/$_fx.log" 2>/dev/null | hex)
    _c=$("$OSR_BIN" ui fail-tail 20 "$FX/$_fx.log" 2>&1 >/dev/null | hex)
    same "fail-tail 20: $_fx" "$_r" "$_c"
done

# --- 9. on a real terminal ---------------------------------------------------
# The §3 auto-degrade decision keys off `[ -t 1 ]`, so the interesting half of
# ui.sh only happens on a pty. `script` gives us one.
if command -v script >/dev/null 2>&1; then
    pty() { script -q -c "$1" /dev/null; }
    _snip='printf "%s|%s\n" "$OSR_RED" "$OSR_DIM"'
    _r=$(pty "env $BASE_ENV sh -c '. \"\$OSR_REF\"; $_snip'" | hex)
    _c=$(pty "env $BASE_ENV sh -c '. \"\$OSR_LIB/ui.sh\"; $_snip'" | hex)
    same "pty: palette is colored on a terminal" "$_r" "$_c"
    _r=$(pty "env $BASE_ENV NO_COLOR=1 sh -c '. \"\$OSR_REF\"; _cursor_hide; _cursor_show'" | hex)
    _c=$(pty "env $BASE_ENV NO_COLOR=1 sh -c '. \"\$OSR_LIB/ui.sh\"; _cursor_hide; _cursor_show'" | hex)
    same "pty: cursor hide/show bytes" "$_r" "$_c"
    # run_step's TTY branch, end to end, on a command that has already exited
    # by the time the window would paint: the collapsed [ok] line is all that
    # is left, and it must match to the byte.
    _rs='run_step "Installing thing" true'
    _r=$(pty "env $BASE_ENV NO_COLOR=1 OSR_LOG=$TMP/ref.log sh -c '. \"\$OSR_REF\"; . \"\$OSR_LIB/log.sh\"; $_rs'" | tail -n 1 | hex)
    _c=$(pty "env $BASE_ENV NO_COLOR=1 OSR_LOG=$TMP/c.log sh -c '. \"\$OSR_LIB/ui.sh\"; . \"\$OSR_LIB/log.sh\"; $_rs'" | tail -n 1 | hex)
    same "pty: run_step collapses to the same [ok] line" "$_r" "$_c"
else
    ok "pty checks skipped (no script(1))"
fi

finish
