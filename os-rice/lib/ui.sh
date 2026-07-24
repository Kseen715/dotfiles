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
    OSR_NC='\033[0m'
else
    OSR_RED='' OSR_GREEN='' OSR_YELLOW='' OSR_CYAN='' OSR_NC=''
fi
export OSR_RED OSR_GREEN OSR_YELLOW OSR_CYAN OSR_NC

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

# _spin <pid> <desc> — animate an ASCII spinner on \r until pid exits. ASCII-only
# program output (§3): the frames render in any TERM/locale, no mojibake.
_spin() {
    _sp_pid=$1
    _sp_desc=$2
    _sp_frames='|/-\'
    _sp_n=${#_sp_frames}
    while kill -0 "$_sp_pid" 2>/dev/null; do
        _sp_i=1
        while [ "$_sp_i" -le "$_sp_n" ]; do
            _sp_c=$(printf '%s' "$_sp_frames" | cut -c "$_sp_i")
            printf '\r%b%s%b %s' "$OSR_CYAN" "$_sp_c" "$OSR_NC" "$_sp_desc"
            kill -0 "$_sp_pid" 2>/dev/null || break
            sleep 0.1
            _sp_i=$((_sp_i + 1))
        done
    done
}

# run_step <desc> <cmd...> — run a step with a spinner on TTY (output hidden to
# $OSR_LOG, dumped on failure), or plain streamed lines when piped/verbose.
run_step() {
    _rs_desc=$1
    shift
    if [ -t 1 ] && [ -z "${OSR_VERBOSE:-}" ]; then
        ( "$@" ) >>"$OSR_LOG" 2>&1 &
        _rs_pid=$!
        _spin "$_rs_pid" "$_rs_desc"
        if wait "$_rs_pid"; then
            printf '\r%b[ok]%b %s\n' "$OSR_GREEN" "$OSR_NC" "$_rs_desc"
        else
            printf '\r%b[!!]%b %s\n' "$OSR_RED" "$OSR_NC" "$_rs_desc"
            tail -n 20 "$OSR_LOG" >&2
            error "$_rs_desc failed"
        fi
    else
        info "$_rs_desc"
        "$@" || error "$_rs_desc failed"
    fi
}
