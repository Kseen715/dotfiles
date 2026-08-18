#!/bin/sh
# Proves lib/theme.sh (now a shim over `osr theme` in the harness core) reads
# every theme exactly as the pure-sh implementation did, frozen at
# test/ref/theme_sh_ref.sh.
#
# Two passes: the REAL themes in this repo (all of them, every field and every
# palette role they declare), and a fixture theme built to break the parser —
# whole-line and trailing comments, a hash that is a color and not a comment,
# tabs, a role whose name prefixes another, a non-hash color value, several
# config: lines, a manifest with no trailing newline. The generated sed script
# (what turns a theme into a palette) is compared in full, which covers the
# arithmetic for {{role_dec}} and {{role_sgr}} as well.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
OSR_REF="$OSR_ROOT/test/ref/theme_sh_ref.sh"; export OSR_REF
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/theme.sh"
. "$HERE/../lib.sh"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

hex() { od -An -tx1 | tr -d ' \n'; }

REF_PRE='. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_REF"'
NEW_PRE='. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/theme.sh"'

_diffs=0
_cases=0
# cmp_call <label> <snippet> — same snippet, both implementations, same $OSR_ROOT.
cmp_call() {
    _cases=$((_cases + 1))
    _r=$(env OSR_ROOT="$OSR_ROOT_UNDER_TEST" sh -c "$REF_PRE; $2" 2>&1 | hex)
    _c=$(env OSR_ROOT="$OSR_ROOT_UNDER_TEST" sh -c "$NEW_PRE; $2" 2>&1 | hex)
    if [ "$_r" != "$_c" ]; then
        _diffs=$((_diffs + 1))
        printf '    diff: %s\n      ref: %s\n      c  : %s\n' "$1" "$_r" "$_c" >&2
    fi
}

# --- 1. every real theme in the repo -----------------------------------------
OSR_ROOT_UNDER_TEST=$OSR_ROOT
cmp_call "osr_themes" 'osr_themes'
for _t in $(osr_themes) nosuchtheme ''; do
    cmp_call "exists $_t" "osr_theme_exists '$_t'; echo rc=\$?"
    cmp_call "session $_t" "osr_theme_session '$_t'"
    cmp_call "configs $_t" "osr_theme_configs '$_t'"
    cmp_call "sed $_t" "_osr_theme_sed '$_t'"
    for _k in display description polarity session gnome_accent no-such-key; do
        cmp_call "meta $_t $_k" "osr_theme_meta '$_t' '$_k'"
    done
    # every role this theme actually declares, plus the near-miss prefixes
    for _role in $(sed -n 's/^ *color: *\([A-Za-z0-9_]*\) .*$/\1/p' \
                   "$OSR_ROOT/themes/$_t/theme.list" 2>/dev/null) accent background nope; do
        cmp_call "color $_t $_role" "osr_theme_color '$_t' '$_role'"
    done
done
for _r in $(ls "$OSR_ROOT/rices"); do
    cmp_call "rice-themes $_r" "osr_rice_themes '$_r'"
    cmp_call "rice-default $_r" "osr_rice_default_theme '$_r'"
done
assert_eq 0 "$_diffs" "real themes: $_cases readings byte-identical"

# --- 2. a fixture theme built to break the parser ----------------------------
_diffs=0
_cases=0
OSR_ROOT_UNDER_TEST="$TMP/tree"
mkdir -p "$TMP/tree/themes/hostile" "$TMP/tree/themes/bare" "$TMP/tree/rices/demo"
cat >"$TMP/tree/themes/hostile/theme.list" <<'EOF'
# a whole-line comment
   # an indented comment
display: Hostile   # trailing comment
description:    spaces   everywhere
polarity:dark
color: background #2e3440
color: background_blur 0
	color: foreground	#d8dee9	
color: accent #88c0d0 # with a comment
color: accent_red #bf616a
color: half #abc
color: text_muted #4c566a
color: surface #3b4252
color: success #a3be8c
color: warning #ebcb8b
color: error #bf616a
color: NotAWord-role #123456
color: novalue
config: gtk-3.0 fontconfig
config: xsettingsd
session: wayland
UPPER: ignored by the generic rule
number9: fine
trailing_hash: value #
EOF
printf 'display: Bare\n' >"$TMP/tree/themes/bare/theme.list"
printf 'theme: hostile\nthemes: hostile bare\nzsh\n' >"$TMP/tree/rices/demo/rice.list"

cmp_call "hostile: themes" 'osr_themes'
cmp_call "hostile: sed script" "_osr_theme_sed hostile"
cmp_call "hostile: configs" "osr_theme_configs hostile"
cmp_call "hostile: session" "osr_theme_session hostile"
cmp_call "bare: session defaults to any" "osr_theme_session bare"
cmp_call "hostile: rice themes" "osr_rice_themes demo"
cmp_call "hostile: rice default" "osr_rice_default_theme demo"
for _k in display description polarity session UPPER number9 trailing_hash missing; do
    cmp_call "hostile: meta $_k" "osr_theme_meta hostile '$_k'"
done
for _role in background background_blur foreground accent accent_red half \
             text_muted surface success warning error NotAWord-role novalue nope; do
    cmp_call "hostile: color $_role" "osr_theme_color hostile '$_role'"
done
# a manifest with no trailing newline must still yield its last directive
printf 'display: NoNewline\ncolor: accent #ffffff' >"$TMP/tree/themes/bare/theme.list"
cmp_call "no trailing newline: meta" "osr_theme_meta bare display"
cmp_call "no trailing newline: color" "osr_theme_color bare accent"
cmp_call "no trailing newline: sed" "_osr_theme_sed bare"
assert_eq 0 "$_diffs" "hostile fixture: $_cases readings byte-identical"

# --- 3. the swatch and the hex arithmetic ------------------------------------
OSR_ROOT_UNDER_TEST=$OSR_ROOT
_diffs=0
_cases=0
for _hexv in '#000000' '#ffffff' '#2e3440' '#88c0d0' '#010203'; do
    cmp_call "hex-dec $_hexv" "_osr_hex_dec '$_hexv'"
done
for _t in $(osr_themes); do
    cmp_call "swatch $_t" "_osr_theme_swatch '$_t'"
done
assert_eq 0 "$_diffs" "palette arithmetic + swatches: $_cases identical"

# --- 4. osr_resolve_theme ----------------------------------------------------
# Non-interactive (no tty in a test): the default theme, the info lines, and
# the exported variables must all match; an unknown name is fatal in both.
for _want in nord '' nosuchtheme; do
    _rrc=0
    env OSR_ROOT="$OSR_ROOT" sh -c "$REF_PRE"'; osr_resolve_theme "$1"; printf "theme=[%s] dir=[%s]\n" "$OSR_THEME" "$OSR_THEME_DIR"' \
        _ "$_want" >"$TMP/ref.out" 2>"$TMP/ref.err" </dev/null || _rrc=$?
    _crc=0
    env OSR_ROOT="$OSR_ROOT" sh -c "$NEW_PRE"'; osr_resolve_theme "$1"; printf "theme=[%s] dir=[%s]\n" "$OSR_THEME" "$OSR_THEME_DIR"' \
        _ "$_want" >"$TMP/c.out" 2>"$TMP/c.err" </dev/null || _crc=$?
    assert_eq "$(hex <"$TMP/ref.out")" "$(hex <"$TMP/c.out")" "resolve_theme '$_want': stdout"
    assert_eq "$(hex <"$TMP/ref.err")" "$(hex <"$TMP/c.err")" "resolve_theme '$_want': stderr"
    assert_eq "$_rrc" "$_crc" "resolve_theme '$_want': exit status ($_rrc)"
done

finish
