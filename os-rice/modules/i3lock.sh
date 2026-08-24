# session: x11
# themable: yes
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
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
#   the idle timer     xidlehook when a Rust toolchain is present, xautolock
#                      otherwise — see the block below
#
# betterlockscreen is packaged on Void and on no Debian/Ubuntu release; apt.map
# routes it to provide_betterlockscreen (lib/build.sh), which installs the
# upstream script. It is not optional either way: it is both the xss-lock target
# and the $mod+Escape binding in the shipped i3 config.
#
# The rice's betterlockscreenrc carries the colors; the wallpaper cache is
# primed here, best-effort, because it needs a running X server.

run_step "Installing lock screen" pkg_install betterlockscreen i3lock-color xss-lock

# The idle timer, and the one real difference between the two options: xautolock
# counts wall-clock idle time and nothing else, so it blanks the screen ten
# minutes into a film. xidlehook's --not-when-audio / --not-when-fullscreen are
# the inhibits every full desktop honours and the fix for i3-sugg §12 gotcha 14.
#
# It is Rust-only (cargo: on both xbps and apt), so it needs `rust` earlier in
# the manifest — which the i3 rices list. Where that toolchain is missing the
# module falls back rather than failing, and the i3 config probes for the binary
# at session start, so both paths produce a working idle lock.
# cargo lives in the TARGET USER's home (modules/rust.sh installs rustup there),
# not on root's PATH, so probe the path _via_cargo itself uses rather than
# `command -v cargo` - which is false under `as_root` even on a machine that has
# Rust. The same applies to the result: cargo installs into ~/.cargo/bin.
_il_cargo="$OSR_HOME/.cargo/bin/cargo"
if as_user test -x "$_il_cargo"; then
    # xidlehook links xcb + libpulse, so the headers have to be there before
    # cargo runs. Only xbps.map and apt.map carry the row (the i3 rices require
    # one of those); an unmapped name passes through unchanged and would try to
    # install a package literally called `xidlehook-build-deps`, so check first
    # rather than let pkg_install hard-fail the module on a third distro.
    if [ "$(_pkgmap_one xidlehook-build-deps)" != xidlehook-build-deps ]; then
        run_step "Installing xidlehook build deps" pkg_install xidlehook-build-deps
    fi
    run_step "Installing xidlehook (idle timer)" pkg_install xidlehook
fi
if as_user test -x "$OSR_HOME/.cargo/bin/xidlehook"; then
    info "xidlehook installed - the idle timer honours audio/fullscreen inhibits"
else
    info "no xidlehook (no Rust toolchain) - installing xautolock instead"
    run_step "Installing xautolock" pkg_install xautolock
fi

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
