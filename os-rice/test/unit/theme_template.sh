#!/bin/sh
# Proves the §6b theme templating in lib/theme.sh + lib/config.sh: a theme is a
# PALETTE, and an app's config is written once as a template that every theme
# renders. This is what took the theme tree off N*M - a file per (theme, app)
# pair - so the assertions here are about that property, not about any one app.
#
# Covered:
#   - color roles (both spellings), meta fields and {{THEME}} all substitute
#   - a role the theme never defines survives as a warning, not a failed switch
#   - the theme's own literal file still wins over the template (escape hatch)
#   - every real theme renders every real template with nothing left unsubstituted
# Hermetic: temp HOME + temp theme root, no root, no packages.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB
NO_COLOR=1; OSR_USER=$(id -un); export OSR_USER   # as_user becomes a no-op
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/user.sh"
. "$OSR_LIB/theme.sh"; . "$OSR_LIB/config.sh"
. "$HERE/../lib.sh"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
OSR_HOME="$TMP/home"; export OSR_HOME
mkdir -p "$OSR_HOME"

# --- a synthetic theme + dotfiles tree, so the test does not track the real one -
FAKE="$TMP/root"; mkdir -p "$FAKE/themes/testtheme"
DOTS="$TMP/dotfiles/someapp"; mkdir -p "$DOTS"
OSR_DOTFILES="$TMP/dotfiles"; export OSR_DOTFILES
cat >"$FAKE/themes/testtheme/theme.list" <<'EOF'
display: Test Theme
polarity: dark
yazi_flavor: test-flavor

color: background  #101010
color: foreground  #f0f0f0
color: accent      #00ff00
EOF
cat >"$DOTS/app.conf.tmpl" <<'EOF'
name = {{THEME}} ({{display}})
polarity = {{polarity}}
flavor = {{yazi_flavor}}
background = {{background}}
foreground = {{foreground}}
accent = {{accent}}
accent_bare = {{accent_rgb}}
EOF

# _osr_theme_sed reads themes/ out of OSR_ROOT, so point it at the fake tree.
REAL_ROOT=$OSR_ROOT
OSR_ROOT=$FAKE
OSR_THEME=testtheme; export OSR_THEME
OSR_THEME_DIR="$FAKE/themes/testtheme"; export OSR_THEME_DIR

render_theme_template "$DOTS/app.conf.tmpl" "$TMP/out.conf" 2>"$TMP/warn"
assert_contains "$TMP/out.conf" '^background = #101010$'     "a color role substitutes"
assert_contains "$TMP/out.conf" '^accent = #00ff00$' "every color role substitutes, not just the first"
assert_contains "$TMP/out.conf" '^polarity = dark$'  "a meta field substitutes too"
assert_contains "$TMP/out.conf" '^flavor = test-flavor$' \
    "a non-color field (yazi's flavor) substitutes - a theme is not only hexes"
assert_contains "$TMP/out.conf" '^name = testtheme (Test Theme)$' \
    "{{THEME}} is the theme's own name, alongside its display field"
assert_contains "$TMP/out.conf" '^accent_bare = 00ff00$' \
    "every color also has an _rgb spelling with no leading hash (foot writes bare RRGGBB)"
refute_contains "$TMP/out.conf" '{{' "nothing is left unsubstituted"
[ -s "$TMP/warn" ] && { cat "$TMP/warn"; fail "a fully-covered template warns"; } \
    || ok "a fully-covered template is silent"

# --- a missing role warns and lands anyway (§9: degrade, never break a switch) --
printf 'missing = {{nosuchrole}}\nbackground = {{background}}\n' >"$DOTS/gap.conf.tmpl"
render_theme_template "$DOTS/gap.conf.tmpl" "$TMP/gap.conf" 2>"$TMP/warn2"
assert_contains "$TMP/gap.conf" '^background = #101010$' "the rest of the file still renders"
assert_contains "$TMP/gap.conf" '{{nosuchrole}}'  "the unknown placeholder is left visible"
assert_contains "$TMP/warn2"    'nosuchrole'      "and it is named in a warning"

# --- the theme's own file wins over the template (the escape hatch) -----------
# No shared app needs this today - every theme renders every template - but the
# precedence is the thing that let the migration land one app at a time, and it is
# the answer for a layer that is genuinely not a palette substitution.
SRC=$(osr_theme_source someapp app.conf)
case "$SRC" in
    *osr-theme-someapp*) ok "with no literal file, the source is a rendered template" ;;
    *) fail "with no literal file, the source is a rendered template (got '$SRC')" ;;
esac
assert_contains "$SRC" '^background = #101010$' "and that rendered file carries the theme's colors"
rm -f "$SRC"
mkdir -p "$OSR_THEME_DIR/config/someapp"
printf 'HANDWRITTEN\n' >"$OSR_THEME_DIR/config/someapp/app.conf"
assert_eq "$OSR_THEME_DIR/config/someapp/app.conf" "$(osr_theme_source someapp app.conf)" \
    "a literal file in the theme wins over the template"
osr_theme_source someapp nosuchfile >/dev/null 2>&1 \
    && fail "an app with neither file nor template returns non-zero" \
    || ok "an app with neither file nor template returns non-zero"

# --- and now against the real tree: every theme renders every template --------
# The regression this catches is a template gaining a placeholder that some theme
# has no value for. It is cheap to add one and easy to forget the other five.
OSR_ROOT=$REAL_ROOT
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd)
unset OSR_THEME_DIR
for _t in $(osr_themes); do
    OSR_THEME=$_t
    for _tmpl in "$OSR_DOTFILES"/*/*.tmpl; do
        [ -f "$_tmpl" ] || continue
        render_theme_template "$_tmpl" "$TMP/real.out" 2>/dev/null
        if grep -q '{{' "$TMP/real.out"; then
            fail "theme $_t leaves $(basename "$_tmpl") with $(sed -n 's/.*{{\([A-Za-z0-9_]*\)}}.*/\1/p' "$TMP/real.out" | sort -u | tr '\n' ' ')unset"
        else
            ok "theme $_t renders $(basename "$_tmpl")"
        fi
    done
done

finish
