# test/lib.sh — tiny assertion helpers for the unit tests (POSIX sh).
# Each test script sources this, runs checks, then calls `finish`.

_T_PASS=0
_T_FAIL=0

ok()   { _T_PASS=$((_T_PASS + 1)); printf '  ok   %s\n' "$*"; }
fail() { _T_FAIL=$((_T_FAIL + 1)); printf '  FAIL %s\n' "$*" >&2; }

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
    printf '  --- %d passed, %d failed ---\n' "$_T_PASS" "$_T_FAIL"
    [ "$_T_FAIL" -eq 0 ]
}
