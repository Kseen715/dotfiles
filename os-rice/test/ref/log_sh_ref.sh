# test/ref/log_sh_ref.sh — the sh implementation of lib/log.sh, FROZEN.
#
# The last pure-sh lib/log.sh, kept verbatim as the specification of what the C
# port (lib/log.c) must print: test/unit/log_c_parity.sh sources this file
# and diffs its bytes against the binary's. Nothing in the installer sources
# it, and it must never be "fixed" — a change here is a change to the reference
# output, not to a live code path.
#
# --- original header ---------------------------------------------------------
#
# lib/log.sh — logging primitives (POSIX sh)
#
# debug / info / warn / error / success / check_error. No color logic here;
# ui.sh owns colors and exports the escape vars. When ui.sh has not been sourced
# these are empty, so log output degrades to plain text.

: "${OSR_RED:=}" "${OSR_GREEN:=}" "${OSR_YELLOW:=}" "${OSR_CYAN:=}" "${OSR_DIM:=}" "${OSR_NC:=}"

info() {
    printf '%b%-8s%b%s\n' "$OSR_CYAN" "[INFO]" "$OSR_NC" "$*"
}

# debug — off unless OSR_DEBUG is set. A theme apply skips dozens of package and
# build steps by design (lib/apply.sh); printing each one would bury the handful
# of lines that say what actually changed, and printing none makes "why did my
# font not update" unanswerable. This is the switch between the two.
debug() {
    [ -n "${OSR_DEBUG:-}" ] || return 0
    printf '%b%-8s%b%s\n' "$OSR_DIM" "[DEBUG]" "$OSR_NC" "$*" >&2
}

warn() {
    printf '%b%-8s%b%s\n' "$OSR_YELLOW" "[WARN]" "$OSR_NC" "$*" >&2
}

success() {
    printf '%b%-8s%b%s\n' "$OSR_GREEN" "[DONE]" "$OSR_NC" "$*"
}

# error prints and terminates the whole run. A single fatal path keeps modules
# from limping on after a mutation half-applied.
error() {
    printf '%b%-8s%b%s\n' "$OSR_RED" "[ERROR]" "$OSR_NC" "$*" >&2
    exit 1
}

# check_error <exit-code> <message> — fatal if the code is non-zero.
check_error() {
    _code=$1
    shift
    [ "$_code" -eq 0 ] || error "$* (exit $_code)"
}
