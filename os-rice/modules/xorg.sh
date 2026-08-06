# session: x11
# modules/xorg.sh — the X11 session core, the X sibling of modules/wayland.sh
# (i3-sugg §1, §13). ONE copy, POSIX, distro-agnostic: logical names carry Arch's
# `xorg-*` spelling and pkgmap translates (Void drops the prefix, Debian bundles
# most of them into x11-xserver-utils/x11-utils).
#
# Three things happen here, and skipping any one of them is a classic "i3 starts
# but nothing works" bug:
#
#   1. server + client utils + input/video drivers   (a session at all)
#   2. dbus + elogind                                (polkit, udisks, lid/idle,
#                                                     portals — all D-Bus)
#   3. ~/.xprofile as a layered loader (§5)          (env every GUI app inherits)
#
# ~/.xinitrc is seeded once (00-env semantics) so `startx` works without a
# display manager; a DM user simply never reads it. It is user territory after
# the first write — os-rice never rewrites it.

run_step "Installing X server + session core" pkg_install \
    xorg-server xorg-xinit xorg-xauth \
    xorg-xrandr xorg-xset xorg-xsetroot xorg-xprop xorg-xev xorg-xkill \
    xorg-xdpyinfo xorg-xinput xdotool \
    xkeyboard-config setxkbmap \
    xf86-input-libinput mesa-dri \
    xorg-fonts-misc fontconfig \
    dbus dbus-x11 elogind polkit libnotify

# D-Bus and elogind must be up before any graphical session: without them polkit
# has no authority to talk to, udisks never auto-mounts, and xss-lock gets no
# suspend/lid signal to hook (§8 — enable_service dispatches per init).
enable_service dbus || warn "could not enable dbus (needs a real init)"
enable_service elogind || warn "could not enable elogind (needs a real init)"

# --- ~/.xprofile: loader block + layered drop-ins (§5) ------------------------
_xp_dir="$OSR_HOME/.config/xprofile.d"
as_user mkdir -p "$_xp_dir"
install_xprofile_loader "$_xp_dir" "$OSR_HOME/.xprofile"

# 10-session.sh — dotfiles-owned, rice-independent (toolkit workarounds, XDG ids)
if [ -f "$OSR_DOTFILES/xprofile/10-session.sh" ]; then
    install_layer "$OSR_DOTFILES/xprofile/10-session.sh" "$_xp_dir/10-session.sh"
fi
# 90-theme.sh — rice-owned, swapped on rice switch (§6)
if [ -n "${OSR_RICE_DIR:-}" ] && [ -f "$OSR_RICE_DIR/config/xprofile/90-theme.sh" ]; then
    install_layer "$OSR_RICE_DIR/config/xprofile/90-theme.sh" "$_xp_dir/90-theme.sh"
fi
# 00-env / 99-local — the user's, never overwritten (created empty, then kept)
seed_empty "$_xp_dir/00-env.sh"
seed_empty "$_xp_dir/99-local.sh"

# --- ~/.xinitrc: startx without a display manager ----------------------------
if [ ! -f "$OSR_HOME/.xinitrc" ]; then
    info "seeding ~/.xinitrc (startx entry point)"
    as_user tee "$OSR_HOME/.xinitrc" >/dev/null <<'EOF'
#!/bin/sh
# Seeded once by os-rice (modules/xorg.sh) — yours to edit, never rewritten.
[ -r "$HOME/.xprofile" ] && . "$HOME/.xprofile"
[ -r /etc/X11/xinit/xinitrc.d ] && for f in /etc/X11/xinit/xinitrc.d/*.sh; do
    [ -r "$f" ] && . "$f"
done
exec dbus-run-session i3
EOF
    as_user chmod +x "$OSR_HOME/.xinitrc"
fi

# --- keyboard layout snippet (root-owned, /etc/X11/xorg.conf.d) --------------
# us,ru with alt+shift toggle, plus tap-to-click and natural scroll on touchpads.
# Written only if absent: a machine's input config is machine territory.
if [ ! -f /etc/X11/xorg.conf.d/30-input.conf ]; then
    info "installing /etc/X11/xorg.conf.d/30-input.conf"
    as_root mkdir -p /etc/X11/xorg.conf.d
    as_root tee /etc/X11/xorg.conf.d/30-input.conf >/dev/null <<'EOF'
# Seeded once by os-rice (modules/xorg.sh).
Section "InputClass"
    Identifier "system-keyboard"
    MatchIsKeyboard "on"
    Option "XkbLayout" "us,ru"
    Option "XkbOptions" "grp:alt_shift_toggle,grp_led:scroll"
EndSection

Section "InputClass"
    Identifier "touchpad"
    MatchIsTouchpad "on"
    Driver "libinput"
    Option "Tapping" "on"
    Option "NaturalScrolling" "true"
    Option "DisableWhileTyping" "true"
EndSection
EOF
fi
