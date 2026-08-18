#!/bin/sh
# Proves lib/log.sh (now a shim over `osr log` in the harness core) prints the exact bytes the
# pure-sh implementation printed, frozen at test/ref/log_sh_ref.sh.
#
# Every one of the five levels is compared on stdout AND stderr separately -
# which stream a line goes to is half of what log.sh decides - across an empty
# palette and a full one, and over messages built to break a sloppy port:
# backslash escapes (the sh printf used `%s` for the message, NOT `%b`),
# percent signs, multiple arguments joined by "$*", and an empty message.
# check_error and error's fatal exit are checked end to end.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
OSR_REF="$OSR_ROOT/test/ref/log_sh_ref.sh"; export OSR_REF
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

# Explicit and identical for both sides: the palette and OSR_DEBUG are exactly
# what log.sh branches on, and this test's own shell has them set.
BASE_ENV='OSR_RED= OSR_GREEN= OSR_YELLOW= OSR_CYAN= OSR_DIM= OSR_NC= OSR_DEBUG= OSR_STEP_N= OSR_STEP_TOTAL='

hex() { od -An -tx1 | tr -d ' \n'; }

same() {
    if [ "$2" = "$3" ]; then
        ok "$1"
    else
        fail "$1"
        printf '    ref: %s\n    c  : %s\n' "$2" "$3" >&2
    fi
}

# --- 1. every level, every stream, palette on and off ------------------------
_cases=0
_diffs=0
for _pal in plain color; do
    case "$_pal" in
        plain) set -- '' '' '' '' '' '' ;;
        color) set -- '\033[0;31m' '\033[0;32m' '\033[0;33m' '\033[0;36m' '\033[2m' '\033[0m' ;;
    esac
    _r1=$1 _g=$2 _y=$3 _c=$4 _d=$5 _n=$6
    for _dbg in '' 1; do
        for _lvl in info warn success debug; do
            for _msg_kind in simple escapes percent multi empty; do
                case "$_msg_kind" in
                    simple)  set -- 'Installing polybar' ;;
                    escapes) set -- 'path C:\new\temp and \033[31m' ;;
                    percent) set -- 'progress %s %d %%' ;;
                    multi)   set -- pkg=zsh 'init=systemd' 'user=alice' ;;
                    empty)   set -- '' ;;
                esac
                for _stream in out err; do
                    _cases=$((_cases + 1))
                    # shellcheck disable=SC2086
                    _rb=$(env $BASE_ENV OSR_DEBUG="$_dbg" \
                        sh -c 'run() { . "$OSR_REF"; OSR_RED=$1 OSR_GREEN=$2 OSR_YELLOW=$3 OSR_CYAN=$4 OSR_DIM=$5 OSR_NC=$6
                                       shift 6; _l=$1; shift; "$_l" "$@"; }
                               run "$@"' _ "$_r1" "$_g" "$_y" "$_c" "$_d" "$_n" "$_lvl" "$@" \
                        2>"$TMP/ref.err" | hex)
                    # shellcheck disable=SC2086
                    _cb=$(env $BASE_ENV OSR_DEBUG="$_dbg" \
                        sh -c 'run() { . "$OSR_LIB/log.sh"; OSR_RED=$1 OSR_GREEN=$2 OSR_YELLOW=$3 OSR_CYAN=$4 OSR_DIM=$5 OSR_NC=$6
                                       export OSR_RED OSR_GREEN OSR_YELLOW OSR_CYAN OSR_DIM OSR_NC
                                       shift 6; _l=$1; shift; "$_l" "$@"; }
                               run "$@"' _ "$_r1" "$_g" "$_y" "$_c" "$_d" "$_n" "$_lvl" "$@" \
                        2>"$TMP/c.err" | hex)
                    if [ "$_stream" = err ]; then
                        _rb=$(hex <"$TMP/ref.err")
                        _cb=$(hex <"$TMP/c.err")
                    fi
                    if [ "$_rb" != "$_cb" ]; then
                        _diffs=$((_diffs + 1))
                        printf '    log diff: pal=%s debug=[%s] level=%s msg=%s stream=%s\n' \
                            "$_pal" "$_dbg" "$_lvl" "$_msg_kind" "$_stream" >&2
                        printf '      ref: %s\n      c  : %s\n' "$_rb" "$_cb" >&2
                    fi
                done
            done
        done
    done
done
assert_eq 0 "$_diffs" "log levels: $_cases stdout/stderr captures byte-identical"

# --- 2. error: the fatal one -------------------------------------------------
# It must print to stderr, print nothing to stdout, and end the shell with 1 -
# the shim keeps the `exit` because a helper process cannot end the run.
for _pal in plain color; do
    case "$_pal" in
        plain) _red='' _nc='' ;;
        color) _red='\033[0;31m' _nc='\033[0m' ;;
    esac
    # shellcheck disable=SC2086
    env $BASE_ENV sh -c '. "$OSR_REF"; OSR_RED=$1; OSR_NC=$2
                         error "module not found: $3"; echo UNREACHABLE' \
        _ "$_red" "$_nc" zsh >"$TMP/ref.out" 2>"$TMP/ref.err" || _rrc=$?
    # shellcheck disable=SC2086
    env $BASE_ENV sh -c '. "$OSR_LIB/log.sh"; OSR_RED=$1; OSR_NC=$2; export OSR_RED OSR_NC
                         error "module not found: $3"; echo UNREACHABLE' \
        _ "$_red" "$_nc" zsh >"$TMP/c.out" 2>"$TMP/c.err" || _crc=$?
    same "error ($_pal): stderr bytes" "$(hex <"$TMP/ref.err")" "$(hex <"$TMP/c.err")"
    same "error ($_pal): stdout bytes" "$(hex <"$TMP/ref.out")" "$(hex <"$TMP/c.out")"
    assert_eq "$_rrc" "$_crc" "error ($_pal): same exit status ($_rrc)"
done

# --- 3. check_error ----------------------------------------------------------
for _code in 0 1 127; do
    # shellcheck disable=SC2086
    env $BASE_ENV sh -c '. "$OSR_REF"; check_error "$1" pkg_install failed; echo SURVIVED' \
        _ "$_code" >"$TMP/ref.out" 2>"$TMP/ref.err" || _rrc=$?
    # shellcheck disable=SC2086
    env $BASE_ENV sh -c '. "$OSR_LIB/log.sh"; check_error "$1" pkg_install failed; echo SURVIVED' \
        _ "$_code" >"$TMP/c.out" 2>"$TMP/c.err" || _crc=$?
    [ "$_code" -eq 0 ] && { _rrc=0; _crc=0; }
    same "check_error $_code: stdout" "$(hex <"$TMP/ref.out")" "$(hex <"$TMP/c.out")"
    same "check_error $_code: stderr" "$(hex <"$TMP/ref.err")" "$(hex <"$TMP/c.err")"
    assert_eq "$_rrc" "$_crc" "check_error $_code: same exit status"
done

# --- 4. the step-counter line install.sh prints ------------------------------
# sh spelled it `info "$(step_prefix)module: $_mod"`; the helper has the
# counter itself. Both must land on the same bytes for a known and an
# unknown total.
for _pair in '0 0' '3 12' '7 7'; do
    # shellcheck disable=SC2086  # deliberate split into "n total"
    set -- $_pair
    # shellcheck disable=SC2086
    _r=$(env $BASE_ENV OSR_STEP_N="$1" OSR_STEP_TOTAL="$2" \
        sh -c '. "$OSR_LIB/ui.sh"; . "$OSR_REF"; info "$(step_prefix)module: zsh"' | hex)
    # shellcheck disable=SC2086
    _c=$(env $BASE_ENV OSR_STEP_N="$1" OSR_STEP_TOTAL="$2" \
        sh -c '. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; "$OSR_BIN" log step "module: zsh"' | hex)
    same "step line: n=$1 total=$2" "$_r" "$_c"
done

# --- 5. no palette at all ----------------------------------------------------
# log.sh is a standalone lib: sourced without ui.sh, every color is empty and
# the output is plain text. That path must survive the rewrite too.
_r=$(env -i PATH="$PATH" HOME="$HOME" OSR_REF="$OSR_REF" \
    sh -c '. "$OSR_REF"; info hello; success done' | hex)
_c=$(env -i PATH="$PATH" HOME="$HOME" OSR_LIB="$OSR_LIB" \
    sh -c '. "$OSR_LIB/log.sh"; info hello; success done' | hex)
same "no ui.sh sourced: plain text" "$_r" "$_c"

finish
