#!/bin/sh
# wallpaper.sh — set or query the wallpaper of the current theme (§6a).
#
#   wallpaper.sh                 print the wallpaper in use
#   wallpaper.sh --list          print the library (theme images + ~/Pictures/Wallpapers)
#   wallpaper.sh <path>          make <path> the wallpaper of the current theme
#   wallpaper.sh --next          step to the next image in the library
#
# Separate from install.sh because it is not an install: no modules run, no
# layers are rewritten. It is the other half of what a picker needs - the theme
# says how things look, this says what is behind them.
set -eu

OSR_ROOT=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_LIB="$OSR_ROOT/lib"
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd)
export OSR_ROOT OSR_LIB OSR_DOTFILES

. "$OSR_LIB/ui.sh"
. "$OSR_LIB/log.sh"
for _lib in detect user config theme state; do
    . "$OSR_LIB/$_lib.sh"
done

usage() {
    sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'
}

OSR_ARG_USER=""
OSR_ACTION=show
OSR_TARGET=""
while [ $# -gt 0 ]; do
    case "$1" in
        --user)    OSR_ARG_USER=${2:?--user needs a name}; shift 2 ;;
        --list)    OSR_ACTION=list; shift ;;
        --next)    OSR_ACTION=next; shift ;;
        -h|--help) usage; exit 0 ;;
        -*)        error "unknown option: $1" ;;
        *)         OSR_ACTION="set"; OSR_TARGET=$1; shift ;;
    esac
done

osr_resolve_user "$OSR_ARG_USER"

# The current theme decides which wallpapers are on offer and which key the
# choice is stored under. With no theme applied yet, fall back to the default.
OSR_THEME=$(osr_state_get theme)
[ -n "$OSR_THEME" ] || OSR_THEME=$OSR_DEFAULT_THEME
osr_theme_exists "$OSR_THEME" || error "recorded theme '$OSR_THEME' no longer exists (see: osr themes)"
OSR_THEME_DIR="$OSR_ROOT/themes/$OSR_THEME"
export OSR_THEME OSR_THEME_DIR

case "$OSR_ACTION" in
    show)
        _cur=$(osr_theme_wallpaper)
        printf '%s\n' "${_cur:-(none)}"
        ;;
    list)
        osr_wallpaper_library
        ;;
    next)
        # Wrap-around step through the library, so a single hotkey can cycle
        # wallpapers without a GUI. Falls back to the first image when the
        # current one is not in the library (or there is no current one).
        _cur=$(osr_theme_wallpaper)
        _cur_base=$(basename "${_cur:-}" 2>/dev/null || true)
        _first=""
        _take=""
        _next=""
        for _img in $(osr_wallpaper_library); do
            [ -n "$_first" ] || _first=$_img
            if [ -n "$_take" ]; then _next=$_img; break; fi
            [ "$(basename "$_img")" = "$_cur_base" ] && _take=1
        done
        [ -n "$_next" ] || _next=$_first
        [ -n "$_next" ] || error "no wallpapers found for theme '$OSR_THEME'"
        osr_choose_wallpaper "$_next" >/dev/null
        printf '%s\n' "$_next"
        ;;
    set)
        [ -e "$OSR_TARGET" ] || error "no such file: $OSR_TARGET"
        osr_choose_wallpaper "$OSR_TARGET" >/dev/null
        success "wallpaper set for theme '$OSR_THEME': $OSR_TARGET"
        ;;
esac
