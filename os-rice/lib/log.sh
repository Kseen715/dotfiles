# lib/log.sh — logging primitives (POSIX sh)
#
# info / warn / error / success / check_error. No color logic here; ui.sh owns
# colors and exports the escape vars. When ui.sh has not been sourced these are
# empty, so log output degrades to plain text.

: "${OSR_RED:=}" "${OSR_GREEN:=}" "${OSR_YELLOW:=}" "${OSR_CYAN:=}" "${OSR_NC:=}"

info() {
    printf '%b%-8s%b%s\n' "$OSR_CYAN" "[INFO]" "$OSR_NC" "$*"
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
