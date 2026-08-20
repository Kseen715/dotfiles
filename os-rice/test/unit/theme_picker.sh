#!/bin/sh
# Proves lib/theme.sh (§6a): themes are first-class objects, independent of any
# rice. Covers discovery, the theme.list metadata/palette parser, the rice ->
# theme directives, and osr_resolve_theme (explicit > menu > default).
# Hermetic: no net, no root, no interactive input.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/config.sh"; . "$OSR_LIB/theme.sh"
. "$HERE/../lib.sh"

# --- discovery ----------------------------------------------------------------
# The default theme must exist as a real theme (the non-TTY fallback target).
osr_theme_exists "$OSR_DEFAULT_THEME" \
    || fail "default theme '$OSR_DEFAULT_THEME' is not a theme"
ok "default theme '$OSR_DEFAULT_THEME' exists"

LIST=$(osr_themes)
_saw_xin=0; _saw_rosemary=0; _n=0
for t in $LIST; do
    _n=$((_n + 1))
    [ -f "$OSR_ROOT/themes/$t/theme.list" ] || fail "osr_themes listed '$t' with no theme.list"
    [ "$t" = "xin" ] && _saw_xin=1
    [ "$t" = "rosemary" ] && _saw_rosemary=1
done
assert_eq 1 "$_saw_xin" "osr_themes includes xin"
assert_eq 1 "$_saw_rosemary" "osr_themes includes rosemary (split out of the i3 rice)"
[ "$_n" -ge 6 ] && ok "osr_themes found $_n themes" || fail "expected >= 6 themes, found $_n"

# Every theme must carry the layers it is made of. Since the §6b migration that
# is the PALETTE, not a config/ tree: an app's theme layer is one template beside
# that app's dotfiles, rendered against these colors, so catppuccin/nord/xin
# ship a theme.list and nothing else and still paint every app. config/ is now
# the escape hatch (a literal file that wins over the template, theme_template.sh)
# and only the themes that need one have it - but an EMPTY config/ is still the
# "lost its files" bug this check was written for, so that stays a failure.
for t in $LIST; do
    _pal=$(grep -c '^color:' "$OSR_ROOT/themes/$t/theme.list" 2>/dev/null || echo 0)
    [ "$_pal" -gt 0 ] || fail "theme '$t' defines no colors"
    if [ -d "$OSR_ROOT/themes/$t/config" ] \
        && [ -z "$(ls -A "$OSR_ROOT/themes/$t/config" 2>/dev/null)" ]; then
        fail "theme '$t' ships an empty config/ dir"
    fi
done
ok "every theme carries a palette (and no empty config/ override tree)"

# --- theme.list metadata + palette --------------------------------------------
assert_eq "Nord" "$(osr_theme_meta nord display)" "display: is read"
assert_eq "dark" "$(osr_theme_meta nord polarity)" "polarity: is read"
assert_eq "any"  "$(osr_theme_session nord)"       "session: any for a portable theme"
assert_eq "x11"  "$(osr_theme_session rosemary)"   "session: x11 for the i3 theme"
assert_eq "wayland" "$(osr_theme_session glass)"   "session: wayland for the hypr theme"
assert_eq "" "$(osr_theme_meta nord no-such-key)"  "an absent key reads as empty"

# session defaults to any when the manifest omits it (a theme is portable until
# it says otherwise, so a new theme is never hidden from the picker by accident).
T=$(mktemp -d)
mkdir -p "$T/themes/bare/config"
printf 'display: Bare\n' > "$T/themes/bare/theme.list"
assert_eq "any" "$(OSR_ROOT=$T osr_theme_session bare)" "session defaults to any"

# Palette: every theme must define the roles Proteus styles itself with, or the
# picker falls back to unreadable defaults on exactly the theme you are choosing.
for t in $LIST; do
    for role in background surface foreground text_dim accent; do
        _c=$(osr_theme_color "$t" "$role")
        case "$_c" in
            "#"[0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F]) ;;
            *) fail "theme '$t' color:$role is not #rrggbb (got '$_c')" ;;
        esac
    done
done
ok "every theme defines background/surface/foreground/text_dim/accent as #rrggbb"
assert_eq "#88c0d0" "$(osr_theme_color nord accent)" "the nord accent is the one starship uses"

# A `color:` role that is a prefix of another must not match it - and with the
# §6b vocabulary that is no longer hypothetical: `accent` sits in front of
# `accent_red`, `background` in front of `background_blur`. The parser anchors on
# the whitespace AFTER the role, not on the role alone.
printf 'color: accent_red #111111\ncolor: accent #222222\n' >> "$T/themes/bare/theme.list"
assert_eq "#222222" "$(OSR_ROOT=$T osr_theme_color bare accent)" "color: accent does not match accent_red"
assert_eq "#bf616a" "$(osr_theme_color nord accent_red)" "and the longer role still reads back"
assert_eq "#2e3440" "$(osr_theme_color nord background)" "background does not swallow background_blur"
assert_eq "0" "$(osr_theme_color nord background_blur)" "background_blur reads its own value"

# --- config: dirs travel with the theme ---------------------------------------
_cfgs=$(osr_theme_configs gruvbox | tr '\n' ' ' | sed 's/ *$//')
assert_eq "gtk-4.0 fontconfig xsettingsd" "$_cfgs" "gruvbox's config: dirs moved into the theme"
assert_eq "" "$(osr_theme_configs nord)" "a theme with no config: line lists nothing"

# --- rice -> theme directives -------------------------------------------------
assert_eq "rosemary" "$(osr_rice_default_theme i3-rosemary)" "rice.list theme: is read"
assert_eq "glass"    "$(osr_rice_default_theme arch-hyprland-glass)" "the hypr rice defaults to glass"
_rt=$(osr_rice_themes i3-rosemary | tr '\n' ' ' | sed 's/ *$//')
assert_eq "rosemary catppuccin nord gruvbox xin" "$_rt" "rice.list themes: is the picker's offer set"

# Every theme a rice offers must exist, or the picker lists a name that cannot
# be applied.
_bad=""
for r in "$OSR_ROOT"/rices/*/; do
    _r=$(basename "$r")
    for t in $(osr_rice_themes "$_r"); do
        osr_theme_exists "$t" || _bad="$_bad $_r:$t"
    done
    _d=$(osr_rice_default_theme "$_r")
    [ -n "$_d" ] || _bad="$_bad $_r:<no-theme:>"
done
assert_eq "" "$_bad" "every rice names an existing default theme and offer set"

# --- osr_resolve_theme --------------------------------------------------------
osr_resolve_theme nord >/dev/null 2>&1
assert_eq "nord" "$OSR_THEME" "explicit name selects that theme"
assert_eq "$OSR_ROOT/themes/nord" "$OSR_THEME_DIR" "OSR_THEME_DIR points at the theme"

# stdin/stdout are redirected here (pipe), so the [ -t 0 ]/[ -t 1 ] check is
# false and resolution takes the non-interactive default branch.
OSR_THEME=""; OSR_THEME_DIR=""
osr_resolve_theme "" >/dev/null 2>&1
assert_eq "$OSR_DEFAULT_THEME" "$OSR_THEME" "no TTY + no name falls back to the default theme"

if ( osr_resolve_theme no-such-theme >/dev/null 2>&1 ); then
    fail "unknown theme should exit non-zero"
else
    ok "unknown theme rejected (exit non-zero)"
fi

rm -rf "$T"
finish
