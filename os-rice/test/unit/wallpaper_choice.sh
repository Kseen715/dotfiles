#!/bin/sh
# Proves per-theme wallpaper selection (§6a): a theme may ship several images,
# the user's pick is remembered PER THEME, and switching away and back restores
# it. Also covers the library (theme images + the user's Wallpapers dir) and the
# degradations - a deleted pick, a placeholder-only theme, a headless box.
#
# Hermetic: temp HOME, temp themes, no display, no root.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT_REAL=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT_REAL/lib"; export OSR_LIB
NO_COLOR=1; OSR_USER=$(id -un); export OSR_USER NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/user.sh"
. "$OSR_LIB/state.sh"; . "$OSR_LIB/config.sh"; . "$OSR_LIB/theme.sh"
. "$HERE/../lib.sh"

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
OSR_HOME="$T/home"; export OSR_HOME
mkdir -p "$OSR_HOME"

# Two themes, one with three images and one with only a placeholder.
OSR_ROOT="$T/root"; export OSR_ROOT
mkdir -p "$OSR_ROOT/themes/alpha/wallpapers" "$OSR_ROOT/themes/beta/wallpapers"
printf 'display: Alpha\n' > "$OSR_ROOT/themes/alpha/theme.list"
printf 'display: Beta\n'  > "$OSR_ROOT/themes/beta/theme.list"
for n in 1-first 2-second 3-third; do printf 'PNG%s' "$n" > "$OSR_ROOT/themes/alpha/wallpapers/$n.png"; done
printf 'drop a real image here\n' > "$OSR_ROOT/themes/beta/wallpapers/README.txt"

# No display server and no setter binary: this test must never paint anything.
DISPLAY=""; WAYLAND_DISPLAY=""; export DISPLAY WAYLAND_DISPLAY

use_theme() {
    OSR_THEME=$1
    OSR_THEME_DIR="$OSR_ROOT/themes/$1"
    export OSR_THEME OSR_THEME_DIR
}

# --- a theme can ship several wallpapers --------------------------------------
use_theme alpha
_n=$(osr_theme_wallpapers | wc -l | tr -d ' ')
assert_eq 3 "$_n" "all three of the theme's images are listed"
assert_eq "$OSR_ROOT/themes/alpha/wallpapers/1-first.png" "$(osr_theme_wallpapers | head -n 1)" \
    "the list is in lexical order, so the default is stable"
assert_eq "$OSR_ROOT/themes/alpha/wallpapers/1-first.png" "$(osr_theme_wallpaper)" \
    "with no choice recorded, the theme's first image is used"

use_theme beta
assert_eq "" "$(osr_theme_wallpaper)" "a placeholder-only theme still resolves to no wallpaper"

# --- choosing is remembered per theme -----------------------------------------
use_theme alpha
INSTALLED=$(osr_choose_wallpaper "$OSR_ROOT/themes/alpha/wallpapers/3-third.png")
assert_eq "$OSR_HOME/Pictures/Wallpapers/3-third.png" "$INSTALLED" \
    "the chosen image is installed into the user's Wallpapers dir"
[ -f "$INSTALLED" ] && ok "the installed copy really exists" || fail "the installed copy really exists"
assert_eq "$OSR_ROOT/themes/alpha/wallpapers/3-third.png" "$(osr_theme_wallpaper)" \
    "the choice is what the theme now resolves to"
assert_contains "$OSR_HOME/.config/osr/state" "wallpaper.alpha=" "the choice is keyed by theme"
assert_contains "$OSR_HOME/.config/osr/wallpaper" "3-third.png" \
    "the bare-path record non-shell consumers read is updated too"

# Switching to another theme must not inherit alpha's pick...
use_theme beta
assert_eq "" "$(osr_theme_wallpaper)" "another theme does not inherit the first theme's choice"

# ...and coming back restores it. This is the whole point of keying by theme.
use_theme alpha
assert_eq "$OSR_ROOT/themes/alpha/wallpapers/3-third.png" "$(osr_theme_wallpaper)" \
    "switching away and back restores the theme's own wallpaper"

# A second theme keeps its own, independent choice.
use_theme beta
mkdir -p "$T/extra"; printf 'PNGextra' > "$T/extra/outside.png"
osr_choose_wallpaper "$T/extra/outside.png" >/dev/null
assert_eq "$T/extra/outside.png" "$(osr_theme_wallpaper)" "an image from outside any theme can be chosen"
use_theme alpha
assert_eq "$OSR_ROOT/themes/alpha/wallpapers/3-third.png" "$(osr_theme_wallpaper)" \
    "the two themes hold independent choices"

# --- degradations -------------------------------------------------------------
# A recorded pick whose file is gone must fall back, not break the apply.
use_theme beta
rm -f "$T/extra/outside.png"
assert_eq "" "$(osr_theme_wallpaper)" "a deleted pick falls back instead of resolving to a missing file"
use_theme alpha
rm -f "$OSR_ROOT/themes/alpha/wallpapers/3-third.png"
assert_eq "$OSR_ROOT/themes/alpha/wallpapers/1-first.png" "$(osr_theme_wallpaper)" \
    "a deleted pick falls back to the theme's default"

# A non-image is refused rather than painted.
printf 'not an image\n' > "$T/notes.txt"
if ( osr_choose_wallpaper "$T/notes.txt" >/dev/null 2>&1 ); then
    fail "a non-image should be refused"
else
    ok "a non-image is refused"
fi

# --- the library ---------------------------------------------------------------
# Theme images first, then everything already installed in ~/Pictures/Wallpapers,
# deduplicated - the dir accretes every image ever applied, across themes.
LIB=$(osr_wallpaper_library)
_libn=$(printf '%s\n' "$LIB" | grep -c . || true)
[ "$_libn" -ge 3 ] && ok "the library spans theme images and the Wallpapers dir ($_libn)" \
    || fail "expected >= 3 library entries, got $_libn"
printf '%s\n' "$LIB" | grep -q "themes/alpha/wallpapers/1-first.png" \
    && ok "the theme's own images come first" || fail "theme images missing from the library"
printf '%s\n' "$LIB" | grep -q "Pictures/Wallpapers/3-third.png" \
    && ok "images installed by an earlier choice stay in the library" \
    || fail "the installed copy is missing from the library"
_dupes=$(printf '%s\n' "$LIB" | while read -r p; do basename "$p"; done | sort | uniq -d)
assert_eq "" "$_dupes" "the library holds no duplicate basenames"

finish
