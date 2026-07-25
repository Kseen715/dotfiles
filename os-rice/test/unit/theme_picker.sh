#!/bin/sh
# Proves osr_resolve_theme_rice (§6): a standalone `osr module` install still
# gets a rice's 90-theme layers. Non-interactive resolution (no TTY) falls back
# to the default rice; an explicit --theme selects a rice and rejects unknowns.
# Hermetic: no net, no root, no interactive input.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/config.sh"
. "$HERE/../lib.sh"

# The default rice must exist as a real, themeable rice (the non-TTY fallback).
[ -d "$OSR_ROOT/rices/$OSR_DEFAULT_THEME_RICE/config" ] \
    || fail "default rice '$OSR_DEFAULT_THEME_RICE' has no config/ dir"

# --- explicit --theme selects that rice --------------------------------------
osr_resolve_theme_rice nord >/dev/null 2>&1
assert_eq "nord" "$OSR_RICE" "explicit --theme selects the named rice"
assert_eq "$OSR_ROOT/rices/nord" "$OSR_RICE_DIR" "OSR_RICE_DIR points at the picked rice"

# --- no --theme, no TTY -> default rice ---------------------------------------
# stdin/stdout are redirected here (pipe), so the [ -t 0 ]/[ -t 1 ] check is
# false and resolution takes the non-interactive default branch.
OSR_RICE=""; OSR_RICE_DIR=""
osr_resolve_theme_rice "" >/dev/null 2>&1
assert_eq "$OSR_DEFAULT_THEME_RICE" "$OSR_RICE" "no TTY + no --theme falls back to default rice"

# --- unknown --theme is a hard error -----------------------------------------
if ( osr_resolve_theme_rice no-such-rice >/dev/null 2>&1 ); then
    fail "unknown --theme should exit non-zero"
else
    ok "unknown --theme rejected (exit non-zero)"
fi

# --- the picker lists exactly the themeable rices ----------------------------
LIST=$(osr_theme_rices)
_saw_xin=0
for r in $LIST; do
    [ -d "$OSR_ROOT/rices/$r/config" ] || fail "osr_theme_rices listed non-themeable '$r'"
    [ "$r" = "xin" ] && _saw_xin=1
done
assert_eq 1 "$_saw_xin" "osr_theme_rices includes xin"

finish
