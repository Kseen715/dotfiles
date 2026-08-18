# lib/log.sh — the shell-callable surface of the logger (POSIX sh)
#
# debug / info / warn / error / success / check_error. The lines are printed by
# `osr log` in the harness core (lib/log.c); what stays here is the shell
# part: error()'s `exit 1` — a separate process can print the message, but only
# this shell can end the run — and the palette defaults, so log output degrades
# to plain text when ui.sh has not been sourced.
#
# Byte-for-byte the sh original, frozen at test/ref/log_sh_ref.sh and diffed by
# test/unit/log_c_parity.sh.

: "${OSR_RED:=}" "${OSR_GREEN:=}" "${OSR_YELLOW:=}" "${OSR_CYAN:=}" "${OSR_DIM:=}" "${OSR_NC:=}"
export OSR_RED OSR_GREEN OSR_YELLOW OSR_CYAN OSR_DIM OSR_NC

# ui.sh resolves $OSR_BIN (and builds it if needed) for every shim; it is
# always sourced first, but this is a standalone lib, so make sure.
if [ -z "${OSR_BIN:-}" ]; then
    . "${OSR_LIB:?log.sh: source lib/ui.sh first, or export OSR_LIB}/ui.sh"
fi

# Each of these took "$*" — every argument joined with a space — so the shim
# joins the same way and hands the core one message.
info()    { "$OSR_BIN" log info "$*"; }
warn()    { "$OSR_BIN" log warn "$*"; }
success() { "$OSR_BIN" log success "$*"; }

# debug — off unless OSR_DEBUG is set (the core makes that call, so the rule
# lives in one place).
debug()   { "$OSR_BIN" log debug "$*"; }

# error prints and terminates the whole run. A single fatal path keeps modules
# from limping on after a mutation half-applied.
error() {
    "$OSR_BIN" log error "$*" || :
    exit 1
}

# check_error <exit-code> <message> — fatal if the code is non-zero.
check_error() {
    _code=$1
    shift
    [ "$_code" -eq 0 ] || error "$* (exit $_code)"
}
