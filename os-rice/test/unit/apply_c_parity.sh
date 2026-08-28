#!/bin/sh
# Proves lib/apply.c derives the same two lists lib/apply.sh does: every
# mutating verb a theme apply neutralizes, and the modules that carry a theme
# layer -- both in manifest order for a known rice, and the overshooting
# whole-tree scan when no rice is recorded.
#
# Both lists are read out of the real tree rather than a fixture: their whole
# point is that they cannot drift from the sources, so a fixture would test the
# fixture. The third piece of apply.sh, osr_apply_stub_mutators, is not ported
# and not compared -- it redefines shell functions for shell modules sourced
# afterwards, which has no C equivalent while those modules are .sh.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
export OSR_BIN
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip apply_c_parity: %s is not built\n' "$OSR_BIN"
    exit 0
fi

TMPOUT=$(mktemp)
trap 'rm -f "$TMPOUT"' EXIT INT TERM

# sh_side <snippet> -- lib/apply.sh in a shell that has what it needs.
sh_side() {
    OSR_ROOT="$OSR_ROOT" OSR_LIB="$OSR_LIB" OSR_BIN="$OSR_BIN" NO_COLOR=1 \
        sh -c '
            . "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/theme.sh"
            . "$OSR_LIB/apply.sh"
            eval "$1"' _ "$1" 2>/dev/null
}

# --- 1. the mutating verb list ------------------------------------------------
SH_VERBS=$(sh_side '_osr_apply_verbs')
C_VERBS=$("$OSR_BIN" apply verbs)
assert_eq "$SH_VERBS" "$C_VERBS" "the same verbs, in the same order"

# Non-vacuous: the list is the reason the stubbing cannot drift, so an empty or
# tiny one would pass the comparison above while proving nothing.
_n=$(printf '%s\n' "$C_VERBS" | grep -c . || :)
if [ "$_n" -gt 50 ]; then ok "the verb list is the real one ($_n names)"
else fail "the verb list is far too short ($_n names)"; fi
printf '%s\n' "$C_VERBS" >"$TMPOUT"
assert_contains "$TMPOUT" '^pkg_install$' "a package verb is in it"
assert_contains "$TMPOUT" '^enable_service$' "a service verb is in it"

# The read-only allowlist is the one thing that must NOT be stubbed: every name
# on it has to be a real function of a mutating lib, or the exception silently
# protects nothing.
for _q in pkg_installed _pkgmap_one service_resolve osr_downloader; do
    assert_contains "$TMPOUT" "^$_q\$" "the query '$_q' is defined by a mutating lib"
done

# --- 2. theme-carrying modules ------------------------------------------------
# Every rice in the tree, so a manifest with require:/theme:/themes: rows and a
# manifest without them are both covered.
for _rl in "$OSR_ROOT"/rices/*/rice.list; do
    [ -f "$_rl" ] || continue
    _rice=$(basename "$(dirname "$_rl")")
    _sh=$(sh_side "osr_theme_modules $_rice")
    _c=$("$OSR_BIN" apply modules "$_rice")
    assert_eq "$_sh" "$_c" "rice '$_rice': the same layers, in manifest order"
    if [ -n "$_c" ]; then ok "rice '$_rice': it found some"
    else fail "rice '$_rice': no theme-carrying module at all"; fi
done

# No rice recorded: the whole-tree scan.
SH_ALL=$(sh_side 'osr_theme_modules ""')
C_ALL=$("$OSR_BIN" apply modules)
assert_eq "$SH_ALL" "$C_ALL" "no rice: the same whole-tree scan"
assert_eq "$SH_ALL" "$("$OSR_BIN" apply modules '')" "an empty rice name is the same as none"

# A rice that does not exist falls back to the whole-tree scan, not to nothing:
# under-painting is the failure mode this list exists to avoid.
assert_eq "$(sh_side 'osr_theme_modules nosuchrice')" \
          "$("$OSR_BIN" apply modules nosuchrice)" "an unknown rice: same fallback"
assert_eq "$C_ALL" "$("$OSR_BIN" apply modules nosuchrice)" \
          "and the fallback is the whole-tree scan"

# The scan overshoots the per-rice list by construction.
_all_n=$(printf '%s\n' "$C_ALL" | grep -c . || :)
if [ "$_all_n" -gt 10 ]; then ok "the whole-tree scan is the real one ($_all_n modules)"
else fail "the whole-tree scan is far too short ($_all_n modules)"; fi

# Sorted, and nothing that is not a module.
assert_eq "$C_ALL" "$(printf '%s\n' "$C_ALL" | LC_ALL=C sort)" "the scan is in sorted order"
_bad=$(printf '%s\n' "$C_ALL" | while IFS= read -r _m; do
    [ -n "$_m" ] || continue
    [ -f "$OSR_ROOT/modules/$_m.sh" ] || printf '%s ' "$_m"
done)
assert_eq "" "$_bad" "every name in the scan is a module"

finish
