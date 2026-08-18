# test/ref/ui_sh_ref.sh — the sh implementation of lib/ui.sh, FROZEN.
#
# This is the last pure-sh lib/ui.sh, kept verbatim as the specification of
# what the C port (lib/ui.c) must print: test/unit/ui_c_parity.sh sources
# this file and diffs its bytes against the binary's. Nothing in the installer
# sources it, and it must never be "fixed" — a change here is a change to the
# reference output, not to a live code path.
#
# --- original header ---------------------------------------------------------
#
# lib/ui.sh — colors, spinner, step progress (POSIX sh)
#
# Everything keys off `[ -t 1 ]` and $OSR_VERBOSE so the same call site is fancy
# on a TTY and clean plain-text when piped to a file or running in CI.

# Colors — emitted only when stdout is a TTY and NO_COLOR is unset, so piped
# logs never carry escape junk (§3 auto-degrade).
if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
    OSR_RED='\033[0;31m'
    OSR_GREEN='\033[0;32m'
    OSR_YELLOW='\033[0;33m'
    OSR_CYAN='\033[0;36m'
    OSR_DIM='\033[2m'
    OSR_NC='\033[0m'
else
    OSR_RED='' OSR_GREEN='' OSR_YELLOW='' OSR_CYAN='' OSR_DIM='' OSR_NC=''
fi
export OSR_RED OSR_GREEN OSR_YELLOW OSR_CYAN OSR_DIM OSR_NC

# Per-run logfile that spinners capture silent output into.
: "${OSR_LOG:=${TMPDIR:-/tmp}/os-rice-$$.log}"
export OSR_LOG

# Step counter — the manifest length is the denominator (§3). install.sh sets
# OSR_STEP_TOTAL before the loop and bumps OSR_STEP_N per module.
: "${OSR_STEP_N:=0}" "${OSR_STEP_TOTAL:=0}"

# step_prefix -> "[03/12] " when a total is known, else "".
step_prefix() {
    [ "$OSR_STEP_TOTAL" -gt 0 ] || { printf ''; return; }
    printf '[%02d/%02d] ' "$OSR_STEP_N" "$OSR_STEP_TOTAL"
}

# --- live step window (docker-build style) -----------------------------------
#
# A running step renders as a block: the last $OSR_TAIL_LINES lines the command
# printed, dimmed, with the step's own spinner line LAST. The block is repainted
# in place, so a long build shows what it is doing instead of a mute spinner, and
# collapses to one `[ok] <desc>` line when it finishes. Same §3 auto-degrade as
# before: TTY only, `--verbose`/pipe/CI still stream plain lines.

: "${OSR_TAIL_LINES:=5}"          # 0 disables the window (spinner line only)

# Terminal width, resolved once. Output lines are cut to it so a wrapped line
# never desyncs the cursor-up arithmetic below.
OSR_COLS=$(command -v tput >/dev/null 2>&1 && tput cols 2>/dev/null || echo '')
case "$OSR_COLS" in ''|*[!0-9]*) OSR_COLS=${COLUMNS:-80} ;; esac
[ "$OSR_COLS" -ge 20 ] || OSR_COLS=80

_OSR_ESC=$(printf '\033')         # real ESC byte, for the sed filter below
_OSR_PAINTED=0                    # lines the last paint left on screen
_OSR_STEP_LOG=""                  # log of the step currently running

# The block repaints several times a second, so a visible cursor blinks all over
# it. Hide it while a step runs, and restore it on the way out - including a
# Ctrl-C or a fatal error(), or the terminal is left with no cursor at all.
_cursor_hide() { [ -t 1 ] && printf '\033[?25l' || :; }
_cursor_show() { [ -t 1 ] && printf '\033[?25h' || :; }
trap '_cursor_show' EXIT
trap '_cursor_show; exit 130' INT TERM

# _step_paint <status-line> — repaint the block in place: dimmed command output,
# then <status-line> last. Runs a few times a second.
# ponytail: re-tails the log every paint; cheap enough vs. a build, revisit only
# if a step's log ever gets big enough for tail to matter.
_step_paint() {
    [ "$_OSR_PAINTED" -gt 0 ] && printf '\033[%dA' "$_OSR_PAINTED"
    _OSR_PAINTED=0
    if [ "$OSR_TAIL_LINES" -gt 0 ] && [ -s "$_OSR_STEP_LOG" ]; then
        _sp_buf=$(tail -n "$OSR_TAIL_LINES" "$_OSR_STEP_LOG" 2>/dev/null \
            | tr -d '\r' | sed "s/${_OSR_ESC}\[[0-9;?]*[A-Za-z]//g" | cut -c "1-$OSR_COLS")
        if [ -n "$_sp_buf" ]; then
            while IFS= read -r _sp_l; do
                printf '\033[2K%b%s%b\n' "$OSR_DIM" "$_sp_l" "$OSR_NC"
                _OSR_PAINTED=$((_OSR_PAINTED + 1))
            done <<EOF
$_sp_buf
EOF
        fi
    fi
    printf '\033[2K%b\n' "$1"
    _OSR_PAINTED=$((_OSR_PAINTED + 1))
}

# _step_done <final-line> — erase the live block and leave one result line.
_step_done() {
    if [ "$_OSR_PAINTED" -gt 0 ]; then
        printf '\033[%dA' "$_OSR_PAINTED"
        _sd_i=0
        while [ "$_sd_i" -lt "$_OSR_PAINTED" ]; do printf '\033[2K\n'; _sd_i=$((_sd_i + 1)); done
        printf '\033[%dA' "$_OSR_PAINTED"
    fi
    _OSR_PAINTED=0
    printf '\033[2K%b\n' "$1"
    _cursor_show
}

# _spin <pid> <desc> — repaint the live block until pid exits. ASCII-only program
# output (§3): the frames render in any TERM/locale, no mojibake.
_spin() {
    _sp_pid=$1
    _sp_desc=$2
    _sp_frames='|/-\'
    _sp_i=1
    _cursor_hide
    while kill -0 "$_sp_pid" 2>/dev/null; do
        _sp_c=$(printf '%s' "$_sp_frames" | cut -c "$_sp_i")
        _step_paint "$(printf '%b%s%b %s' "$OSR_CYAN" "$_sp_c" "$OSR_NC" "$_sp_desc")"
        sleep 0.2
        _sp_i=$((_sp_i % 4 + 1))
    done
}

# run_step <desc> <cmd...> — run a step with the live window on TTY (output kept
# in $OSR_LOG, tail dumped on failure), or plain streamed lines when piped/verbose.
run_step() {
    _rs_desc=$1
    shift
    if [ -t 1 ] && [ -z "${OSR_VERBOSE:-}" ]; then
        # Per-step log so the window shows THIS step's output, not the whole run;
        # appended to $OSR_LOG afterwards so the run log stays complete.
        _OSR_STEP_LOG="$OSR_LOG.step"
        : >"$_OSR_STEP_LOG"
        ( "$@" ) >>"$_OSR_STEP_LOG" 2>&1 &
        _rs_pid=$!
        _spin "$_rs_pid" "$_rs_desc"
        if wait "$_rs_pid"; then _rs_rc=0; else _rs_rc=$?; fi
        cat "$_OSR_STEP_LOG" >>"$OSR_LOG"
        if [ "$_rs_rc" -eq 0 ]; then
            _step_done "$(printf '%b[ok]%b %s' "$OSR_GREEN" "$OSR_NC" "$_rs_desc")"
        else
            _step_done "$(printf '%b[!!]%b %s' "$OSR_RED" "$OSR_NC" "$_rs_desc")"
            tail -n 20 "$_OSR_STEP_LOG" >&2
            error "$_rs_desc failed"
        fi
    else
        info "$_rs_desc"
        "$@" || error "$_rs_desc failed"
    fi
}
