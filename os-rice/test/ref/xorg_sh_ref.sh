# test/ref/xorg_sh_ref.sh — the sh implementation of modules/xorg.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/xorg.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11
# themable: yes
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
    dbus dbus-x11 polkit libnotify

# D-Bus and a seat/login manager must be up before any graphical session: without
# them polkit has no authority to talk to, udisks never auto-mounts, and xss-lock
# gets no suspend/lid signal to hook (§8 — enable_service dispatches per init).
enable_service dbus || warn "could not enable dbus (needs a real init)"

# elogind is systemd-logind carved out for the inits that have no systemd. On a
# systemd host it is not merely redundant: apt resolves `elogind` by REMOVING
# systemd-sysv (they both own /run/systemd/seats and Provides: logind), which
# takes the running init with it. So the package is chosen by init, not by distro.
if [ "${OSR_INIT:-}" = systemd ]; then
    info "systemd provides logind - skipping elogind"
else
    run_step "Installing elogind (seat/login manager)" pkg_install elogind
    enable_service elogind || warn "could not enable elogind (needs a real init)"
fi

# --- ~/.xprofile: loader block + layered drop-ins (§5) ------------------------
_xp_dir="$OSR_HOME/.config/xprofile.d"
as_user mkdir -p "$_xp_dir"
install_xprofile_loader "$_xp_dir" "$OSR_HOME/.xprofile"

# 10-session.sh — dotfiles-owned, rice-independent (toolkit workarounds, XDG ids)
if [ -f "$OSR_DOTFILES/xprofile/10-session.sh" ]; then
    install_layer "$OSR_DOTFILES/xprofile/10-session.sh" "$_xp_dir/10-session.sh"
fi
# 90-theme.sh — rice-owned, swapped on rice switch (§6)
if install_theme_layer xprofile 90-theme.sh "$_xp_dir/90-theme.sh"; then
    :
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

# --- GPU quirks (root-owned, /etc/X11/xorg.conf.d) ---------------------------
# Two things that take the whole X SERVER down rather than just an app, both on
# old laptops, both invisible until they happen.
#
# 1. glamor on a pre-GL-3 GPU. The modesetting driver accelerates 2D through
#    glamor, which is OpenGL - and on gen4/gen5 Intel (Ironlake and older) Mesa
#    can only offer it a GL 2.1 context. Recent Mesa aborts() out of gallium in
#    that configuration, and because glamor runs inside the server, the abort IS
#    an X crash: every window dies at once and you land back at the greeter.
#    Observed as `Caught signal 6 (Aborted)` with libgallium/libglamoregl in the
#    backtrace. Turning accel off costs software 2D and nothing else - client GL
#    still goes direct to the kernel via DRI3, so this does not affect apps.
#
# 2. A second GPU screen on a hybrid-graphics laptop. Xorg auto-adds the
#    discrete GPU as a secondary screen and starts a SECOND glamor stack on it
#    (nouveau on the NVIDIA half), doubling the surface area for the same class
#    of crash - for a GPU that is not driving the panel. AutoAddGPU off leaves
#    the discrete card alone; it also stops it being woken for nothing, which on
#    a laptop is heat and battery.
#
# The glamor half is gated on EVIDENCE, not on a table of PCI IDs: the Xorg log
# states the context it got, so a machine that reported GL 1.x/2.x to glamor is
# exactly the machine that must not use it. On a first-ever install there is no
# log yet, nothing is written, and a later `osr module xorg` picks it up.
_xq_conf=/etc/X11/xorg.conf.d/20-gpu-quirks.conf
_xq_body=""

# Three signatures, any one of which condemns glamor on this machine, plus a
# manual override. One signature was not enough: this fired on a laptop that
# was demonstrably crashing, wrote only the AutoAddGPU half, and left glamor
# enabled - the evidence lives in a log that rotates, so a single grep against a
# single moment is a coin flip. Say WHICH one matched, so a run that does
# nothing can be told apart from a run that found nothing.
_xq_why=""
if [ "${OSR_X_DISABLE_GLAMOR:-0}" = 1 ]; then
    _xq_why="OSR_X_DISABLE_GLAMOR=1 was set"
elif grep -qs 'glamor: Using OpenGL [012]\.' /var/log/Xorg.0.log /var/log/Xorg.0.log.old; then
    _xq_why="glamor reported a pre-GL-3 context in the Xorg log"
elif grep -qs 'libglamoregl' /var/log/Xorg.0.log.old /var/log/Xorg.0.log; then
    # A recorded backtrace through glamor IS the crash this option prevents.
    _xq_why="a previous X crash has glamor in its backtrace"
elif grep -qs 'Caught signal 6' /var/log/Xorg.0.log.old; then
    _xq_why="the X server aborted (signal 6) in a previous session"
fi

if [ -n "$_xq_why" ]; then
    warn "disabling X 2D acceleration: $_xq_why (glamor aborts the whole server here)"
    # A Device section, NOT an OutputClass. OutputClass forwards a small fixed
    # set of keys (PrimaryGPU, Driver, ModulePath, ...); AccelMethod is a Device
    # option and is simply dropped there. Measured: with the OutputClass form in
    # place the server still logged "glamor X acceleration enabled on ILK".
    # Without a BusID this matches the first device the driver claimed, which
    # with AutoAddGPU off is the only one.
    _xq_body="$_xq_body"'Section "Device"
    Identifier "osr-gpu0"
    Driver "modesetting"
    Option "AccelMethod" "none"
EndSection

'
fi

if [ "${OSR_GPU_COUNT:-1}" -gt 1 ]; then
    info "hybrid graphics (${OSR_GPU_COUNT} GPUs) - not auto-adding the discrete GPU as a second X screen"
    _xq_body="$_xq_body"'Section "ServerFlags"
    Option "AutoAddGPU" "off"
EndSection

'
fi

if [ -n "$_xq_body" ]; then
    if [ -f "$_xq_conf" ] && [ "$(cat "$_xq_conf" 2>/dev/null)" = "# Managed by os-rice (modules/xorg.sh) - GPU stability quirks.
$_xq_body" ]; then
        info "$_xq_conf already current, skipping"
    else
        info "installing $_xq_conf"
        as_root mkdir -p /etc/X11/xorg.conf.d
        printf '# Managed by os-rice (modules/xorg.sh) - GPU stability quirks.\n%s' "$_xq_body" |
            as_root tee "$_xq_conf" >/dev/null
        warn "X must be restarted (log out) for $_xq_conf to take effect"
    fi
elif [ -f "$_xq_conf" ]; then
    info "no GPU quirks needed on this machine - removing $_xq_conf"
    as_root rm -f "$_xq_conf"
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
