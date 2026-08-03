# test/lib.sh — tiny assertion helpers for the unit tests (POSIX sh).
# Each test script sources this, runs checks, then calls `finish`.

_T_PASS=0
_T_FAIL=0

# Harness colors. Each test sets NO_COLOR=1 before sourcing lib/ui.sh so the code
# under test stays plain, which also blanks OSR_RED/GREEN - so run.sh passes the
# real terminal decision in OSR_TEST_COLOR (standalone runs re-derive it here).
if [ -z "${OSR_TEST_COLOR+set}" ]; then      # standalone run: decide it here
    OSR_TEST_COLOR=''
    [ -t 1 ] && OSR_TEST_COLOR=1
fi
if [ -n "$OSR_TEST_COLOR" ]; then
    _T_G='\033[0;32m'; _T_R='\033[0;31m'; _T_NC='\033[0m'
else
    _T_G='' _T_R='' _T_NC=''
fi

ok()   { _T_PASS=$((_T_PASS + 1)); printf '  %bok%b   %s\n' "$_T_G" "$_T_NC" "$*"; }
fail() { _T_FAIL=$((_T_FAIL + 1)); printf '  %bFAIL%b %s\n' "$_T_R" "$_T_NC" "$*" >&2; }

# assert_contains <file> <pattern> <label>
assert_contains() {
    if grep -q "$2" "$1" 2>/dev/null; then ok "$3"; else fail "$3 (missing '$2' in $1)"; fi
}

# refute_contains <file> <pattern> <label>
refute_contains() {
    if grep -q "$2" "$1" 2>/dev/null; then fail "$3 (unexpected '$2' in $1)"; else ok "$3"; fi
}

# assert_eq <expected> <actual> <label>
assert_eq() {
    if [ "$1" = "$2" ]; then ok "$3"; else fail "$3 (expected '$1', got '$2')"; fi
}

finish() {
    [ "$_T_FAIL" -eq 0 ] && _t_c=$_T_G || _t_c=$_T_R
    printf '  %b--- %d passed, %d failed ---%b\n' "$_t_c" "$_T_PASS" "$_T_FAIL" "$_T_NC"
    [ "$_T_FAIL" -eq 0 ]
}
