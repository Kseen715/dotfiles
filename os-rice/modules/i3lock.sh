# session: x11
# modules/i3lock.sh — screen lock + idle, the X11 replacement for hyprlock and
# hypridle (i3-sugg §2).
#
# Three cooperating pieces, and the middle one is the piece people forget:
#
#   betterlockscreen  the lock UI — caches a blurred/dimmed copy of the wallpaper
#                     once, so unlocking is instant instead of a 1s blur
#   xss-lock          binds the lock to the X screensaver AND to the logind/
#                     elogind suspend inhibitor: `--transfer-sleep-lock` is what
#                     makes the screen actually be locked when the lid opens
#   xautolock         the idle timer itself (xidlehook is the fancier option —
#                     --not-when-audio/--not-when-fullscreen — but it is not
#                     packaged on Void; see i3-void-packages.md)
#
# The rice's betterlockscreenrc carries the colors; the wallpaper cache is
# primed here, best-effort, because it needs a running X server.

run_step "Installing lock screen" pkg_install betterlockscreen i3lock-color xss-lock xautolock

if [ -n "${OSR_THEME_DIR:-}" ] && [ -f "$OSR_THEME_DIR/config/betterlockscreen/betterlockscreenrc" ]; then
    install_layer "$OSR_THEME_DIR/config/betterlockscreen/betterlockscreenrc" \
        "$OSR_HOME/.config/betterlockscreen/betterlockscreenrc"
fi

# Prime the blur cache from this rice's wallpaper. Needs X, so it degrades to a
# note when headless (§9) — betterlockscreen re-caches on first use anyway.
_bl_wp=$(osr_install_wallpaper)
if [ -n "$_bl_wp" ] && [ -n "${DISPLAY:-}" ] && command -v betterlockscreen >/dev/null 2>&1; then
    run_step "Caching lock screen wallpaper" \
        as_user betterlockscreen -u "$_bl_wp" --fx dimblur || \
        warn "betterlockscreen cache failed - it will rebuild on first lock"
elif [ -n "$_bl_wp" ]; then
    info "no DISPLAY - skipping lock screen cache (run: betterlockscreen -u '$_bl_wp')"
fi
