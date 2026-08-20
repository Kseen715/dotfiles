# lib/ui.sh — the shell-callable surface of the UI (POSIX sh)
#
# Not an implementation: `osr ui` in the harness core (build/osr, from
# lib/ui.c) paints every byte. This file exists for one reason — run_step's
# arguments are shell FUNCTIONS (`pkg_install`, `as_root`, `osr_install_nerd_font`),
# so only a shell can fork them — and it carries the shell-level state that goes
# with that: the exported palette, the step counter, and the EXIT/INT traps that
# give the cursor back.
#
# It is also where every other shim gets $OSR_BIN from, being the file all of
# them are sourced after.
#
# Behavior is byte-for-byte the sh original's, frozen at test/ref/ui_sh_ref.sh
# and diffed by test/unit/ui_c_parity.sh. The §3 auto-degrade is unchanged:
# everything keys off `[ -t 1 ]` and $OSR_VERBOSE, so the same call site is
# fancy on a TTY and clean plain-text when piped to a file or running in CI.

# --- the harness core --------------------------------------------------------
#
# sh cannot ask where a sourced file lives, so take OSR_LIB when the caller
# exported it (install.sh, the unit tests) and otherwise derive it from the
# sourcing script's $0 (osr, wallpaper.sh, test/lint.sh).
if [ -z "${OSR_BIN:-}" ]; then
    for _osr_d in "${OSR_LIB:-}" "$(dirname -- "$0")/lib" "$(dirname -- "$0")/../lib" "$(dirname -- "$0")"; do
        if [ -n "$_osr_d" ] && [ -f "$_osr_d/ui.sh" ]; then
            OSR_ROOT=${OSR_ROOT:-$(cd -- "$_osr_d/.." && pwd)}
            break
        fi
    done
    if [ ! -d "${OSR_ROOT:-}" ]; then
        printf '[ERROR] ui.sh: cannot locate the os-rice tree from "%s" - export OSR_LIB\n' "$0" >&2
        exit 1
    fi
    OSR_BIN="$OSR_ROOT/build/osr"
    # nob.c is the build system (its header comment has the one-time bootstrap
    # line); ./osr and osr.ps1 both just run it. Do the same here, so sourcing
    # this file in a fresh checkout works before anyone has built anything.
    if [ ! -x "$OSR_BIN" ]; then
        if [ ! -x "$OSR_ROOT/build/nob" ]; then
            mkdir -p "$OSR_ROOT/build" 2>/dev/null || :
            # shellcheck disable=SC2086  # $CC is a command line ("zig cc"), not one word
            ${CC:-cc} -o "$OSR_ROOT/build/nob" "$OSR_ROOT/nob.c" || {
                printf '[ERROR] ui.sh: cannot bootstrap nob with %s\n' "${CC:-cc}" >&2
                exit 1
            }
        fi
        ( cd -- "$OSR_ROOT" && ./build/nob ) >/dev/null || {
            printf '[ERROR] ui.sh: nob could not build %s\n' "$OSR_BIN" >&2
            ( cd -- "$OSR_ROOT" && ./build/nob ) >&2
            exit 1
        }
    fi
    export OSR_BIN OSR_ROOT
fi

# --- colors + terminal width -------------------------------------------------
#
# Emitted only when stdout is a TTY and NO_COLOR is unset, so piped logs never
# carry escape junk (§3). The values stay LITERAL '\033[...' — the printf '%b'
# in the core expands them.
#
# fd 3 is this shell's real stdout: inside the `$(...)` below fd 1 is the
# capture pipe, so the core looks at fd 3 to make the very same `[ -t 1 ]` call
# the sh original made. Closed again immediately.
exec 3>&1
eval "$("$OSR_BIN" ui vars)"
exec 3>&-
export OSR_RED OSR_GREEN OSR_YELLOW OSR_CYAN OSR_DIM OSR_NC

# Per-run logfile that spinners capture silent output into.
: "${OSR_LOG:=${TMPDIR:-/tmp}/os-rice-$$.log}"
export OSR_LOG

# Step counter — the manifest length is the denominator (§3). install.sh sets
# OSR_STEP_TOTAL before the loop and bumps OSR_STEP_N per module. Exported,
# along with the window height, because the core reads all three from the
# environment.
: "${OSR_STEP_N:=0}" "${OSR_STEP_TOTAL:=0}" "${OSR_TAIL_LINES:=5}"
export OSR_STEP_N OSR_STEP_TOTAL OSR_TAIL_LINES

# step_prefix -> "[03/12] " when a total is known, else "".
step_prefix() { "$OSR_BIN" ui step-prefix; }

# The live block repaints several times a second, so a visible cursor blinks all
# over it. The core hides it while a step runs; restore it on the way out —
# including a Ctrl-C or a fatal error(), or the terminal is left with no cursor.
_cursor_hide() { "$OSR_BIN" ui cursor-hide; }
_cursor_show() { "$OSR_BIN" ui cursor-show; }
trap '_cursor_show' EXIT
trap '_cursor_show; exit 130' INT TERM

# run_step <desc> <cmd...> — run a step with the live window on TTY (output kept
# in $OSR_LOG, tail dumped on failure), or plain streamed lines when piped/verbose.
#
# The fork stays here and the paint loop runs beside it: `ui spin` watches the
# pid with kill(pid, 0) and leaves the row count of the block it painted in the
# state file, which `ui result` then erases and replaces with one result line.
run_step() {
    _rs_desc=$1
    shift
    if "$OSR_BIN" ui tty-mode; then
        # Per-step log so the window shows THIS step's output, not the whole run;
        # appended to $OSR_LOG afterwards so the run log stays complete.
        _OSR_STEP_LOG="$OSR_LOG.step"
        : >"$_OSR_STEP_LOG"
        ( "$@" ) >>"$_OSR_STEP_LOG" 2>&1 &
        _rs_pid=$!
        "$OSR_BIN" ui spin "$_rs_pid" "$_rs_desc" "$_OSR_STEP_LOG" "$OSR_LOG.paint"
        if wait "$_rs_pid"; then _rs_rc=0; else _rs_rc=$?; fi
        cat "$_OSR_STEP_LOG" >>"$OSR_LOG"
        if [ "$_rs_rc" -eq 0 ]; then
            "$OSR_BIN" ui result "$OSR_LOG.paint" ok "$_rs_desc"
        else
            "$OSR_BIN" ui result "$OSR_LOG.paint" fail "$_rs_desc"
            "$OSR_BIN" ui fail-tail 20 "$_OSR_STEP_LOG"
            error "$_rs_desc failed"
        fi
    else
        info "$_rs_desc"
        "$@" || error "$_rs_desc failed"
    fi
}

# try_step <desc> <cmd...> — run_step for a step that is allowed to fail.
#
# Same live window, same greyed tail, same collapse to one line — the whole
# point is that an OPTIONAL package install looks like every other step instead
# of dumping several hundred lines of the package manager's own chatter into
# the middle of a run. It differs from run_step in exactly one place: a non-zero
# exit ends as `[--] <desc>` and is returned to the caller, rather than as a red
# failure that `error` turns into the end of the run.
#
# Returns the command's exit status, so a caller can still say what it lost.
try_step() {
    _ts_desc=$1
    shift
    if "$OSR_BIN" ui tty-mode; then
        _OSR_STEP_LOG="$OSR_LOG.step"
        : >"$_OSR_STEP_LOG"
        ( "$@" ) >>"$_OSR_STEP_LOG" 2>&1 &
        _ts_pid=$!
        "$OSR_BIN" ui spin "$_ts_pid" "$_ts_desc" "$_OSR_STEP_LOG" "$OSR_LOG.paint"
        if wait "$_ts_pid"; then _ts_rc=0; else _ts_rc=$?; fi
        cat "$_OSR_STEP_LOG" >>"$OSR_LOG"
        if [ "$_ts_rc" -eq 0 ]; then
            "$OSR_BIN" ui result "$OSR_LOG.paint" ok "$_ts_desc"
        else
            # No fail-tail: the step is optional, so the log is where the
            # detail belongs. The caller's warn() says what was skipped.
            "$OSR_BIN" ui result "$OSR_LOG.paint" warn "$_ts_desc"
        fi
        return $_ts_rc
    fi
    info "$_ts_desc"
    "$@"
}
