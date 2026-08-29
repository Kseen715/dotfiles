#!/bin/sh
# Proves the `osr` front end's verbs still resolve. It is a dispatcher — every
# arm either execs another entry point or asks the core a question — so what
# can rot in it is a path, and nothing else was checking any of them.
#
# That is not hypothetical: `osr theme` with no name sourced lib/state.sh for
# years after lib/state.c replaced it and the file was deleted, and the verb
# had been dead the whole time.
#
# Hermetic: only the read-only verbs are run (the listings and the two
# questions), with $HOME pointed at a sandbox so the state file is the
# fixture's. Nothing here installs, paints or escalates.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP/home/.config/osr"

# osr <args...> — the front end, with a sandbox HOME.
osr() {
    env HOME="$TMP/home" OSR_HOME="$TMP/home" NO_COLOR=1 TERM=dumb \
        sh "$OSR_ROOT/osr" "$@" </dev/null 2>&1
}

# --- the listings -------------------------------------------------------------
for _v in list themes modules; do
    _out=$(osr "$_v") || fail "osr $_v exited non-zero"
    case "$_out" in
        ?*) ok "osr $_v lists something" ;;
        *)  fail "osr $_v printed nothing" ;;
    esac
done
assert_eq "$(osr list)" "$(sh "$OSR_ROOT/install.sh" --list)" "osr list is install.sh --list"
assert_eq "$(osr modules)" "$(sh "$OSR_ROOT/install.sh" --list-modules)" \
    "osr modules is install.sh --list-modules"

# --- the two questions --------------------------------------------------------
# `osr theme` with no name prints the applied theme, or says none is.
assert_eq "(none applied)" "$(osr theme)" "osr theme with nothing applied"
printf 'theme=nord\n' >"$TMP/home/.config/osr/state"
assert_eq "nord" "$(osr theme)" "osr theme reads the recorded theme"
# ...and an option is a question too, not a theme name: `osr theme --no-reload`
# must not try to apply a theme called "--no-reload".
assert_eq "nord" "$(osr theme --no-reload)" "osr theme <option> is still the question"

# --- the help -----------------------------------------------------------------
assert_contains_str() { case "$1" in *"$2"*) ok "$3" ;; *) fail "$3" ;; esac; }
# No verb at all, and -h, are the same answer: the usage block, exit 0.
for _a in "" -h --help; do
    _rc=0
    _out=$(osr $_a) || _rc=$?
    assert_contains_str "$_out" "osr install" "osr ${_a:-<no verb>} prints its usage"
    assert_eq 0 "$_rc" "osr ${_a:-<no verb>} exits 0"
done
# ...and an unknown verb is an error naming the ones that exist.
_rc=0
_out=$(osr no-such-verb) || _rc=$?
assert_contains_str "$_out" "unknown command" "an unknown verb is reported"
assert_eq 1 "$_rc" "an unknown verb exits 1"

# --- every verb the usage block names is a real arm ---------------------------
# The header is the documentation people read; a verb listed there and missing
# from the case is the same class of bug as the one above.
_missing=""
for _v in install switch theme wallpaper module list themes modules benchmark \
          undervolt test; do
    grep -q "^    $_v)" "$OSR_ROOT/osr" \
        || grep -q "^    $_v|" "$OSR_ROOT/osr" \
        || grep -q "|$_v)" "$OSR_ROOT/osr" \
        || _missing="$_missing $_v"
done
assert_eq "" "$_missing" "every documented verb has an arm in the dispatcher"

finish
