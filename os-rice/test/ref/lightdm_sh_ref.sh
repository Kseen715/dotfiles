# test/ref/lightdm_sh_ref.sh — the sh implementation of modules/lightdm.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/lightdm.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11
# themable: yes
# modules/lightdm.sh — LightDM display manager + GTK greeter (i3-sugg §1.4).
# The lighter alternative to modules/sddm.sh for an X11 rice: no Qt, one config
# file, and it runs the PAM stack that unlocks the keyring at login (which
# `startx` does not).
#
# The greeter theme is rice-owned but lives in /etc (root), not ~/.config —
# the greeter runs as its own user before yours exists, so it cannot read your
# home. That is why this is the one theme layer written with as_root.
#
# Three things are installed here and the last two are what make the boot look
# like a boot into a desktop rather than a boot into a console:
#
#   1. lightdm + lightdm-gtk-greeter                (a display manager at all)
#   2. /etc/lightdm/lightdm.conf.d/10-osr.conf      (WHICH vt, WHICH session)
#   3. the greeter theme: conf + user CSS           (§6b, palette-driven)

run_step "Installing LightDM" pkg_install lightdm lightdm-gtk-greeter

# --- which session the greeter logs you into ---------------------------------
# Left unset, lightdm picks whatever .desktop sorts first in /usr/share/xsessions
# and that is rarely the rice's WM. Prefer i3, fall back to the first entry that
# actually exists rather than naming a session this machine cannot start.
_ld_session=
for _ld_c in i3 i3-with-shmlog openbox xfce; do
    if [ -f "/usr/share/xsessions/$_ld_c.desktop" ]; then _ld_session=$_ld_c; break; fi
done
if [ -z "$_ld_session" ]; then
    for _ld_f in /usr/share/xsessions/*.desktop; do
        [ -f "$_ld_f" ] || continue
        _ld_session=$(basename "$_ld_f" .desktop); break
    done
fi

# --- the boot handoff: greeter on vt1, not behind a login prompt -------------
# LightDM's built-in minimum-vt is 7, so on every init that starts a getty on
# tty1 the console wins the race: you get `login:` on tty1 and the greeter
# appears on tty7 seconds later, once dbus/elogind and the greeter are up. That
# is the "booted to CLI, then LightDM opened" symptom, and it is a vt choice,
# not a slow service.
#
# The fix is one file plus, per init, making sure nothing else owns vt1.
# conf.d is read BEFORE /etc/lightdm/lightdm.conf, so a distro that spells
# minimum-vt out in that file would still win — those keys are commented out
# below (with a marker, and a .bak kept) rather than fought with.
info "pointing LightDM at vt1 (greeter instead of a console login)"
as_root mkdir -p /etc/lightdm/lightdm.conf.d
{
    printf '%s\n' \
        '# Written by os-rice (modules/lightdm.sh). Edit lightdm.conf instead:' \
        '# it is read after this directory and overrides everything here.' \
        '[LightDM]' \
        'minimum-vt=1' \
        '' \
        '[Seat:*]' \
        'greeter-session=lightdm-gtk-greeter'
    [ -n "$_ld_session" ] && printf 'user-session=%s\n' "$_ld_session"
} | as_root tee /etc/lightdm/lightdm.conf.d/10-osr.conf >/dev/null

if [ -f /etc/lightdm/lightdm.conf ]; then
    if grep -qE '^[[:space:]]*(minimum-vt|greeter-session|user-session)[[:space:]]*=' /etc/lightdm/lightdm.conf; then
        [ -f /etc/lightdm/lightdm.conf.bak ] \
            || as_root cp -f /etc/lightdm/lightdm.conf /etc/lightdm/lightdm.conf.bak
        info "commenting out minimum-vt/greeter-session/user-session in lightdm.conf (10-osr.conf owns them)"
        as_root sed -i -E \
            's/^[[:space:]]*(minimum-vt|greeter-session|user-session)[[:space:]]*=/#osr# &/' \
            /etc/lightdm/lightdm.conf
    fi
fi

# Nothing else may hold vt1, or the greeter and a getty repaint over each other.
case "${OSR_INIT:-}" in
    runit)
        # Void enables agetty-tty1 by default. tty2..tty6 stay, so a greeter that
        # fails to start still leaves a way in.
        _ld_rundir=${OSR_SERVICE_DIR:-/var/service}
        if [ -e "$_ld_rundir/agetty-tty1" ]; then
            info "disabling agetty-tty1 (LightDM owns vt1; tty2-tty6 unchanged)"
            disable_service agetty-tty1
        fi ;;
    systemd)
        # Debian's lightdm.service already conflicts with getty@tty1; a drop-in
        # makes that true on any host and is a no-op where it is already set.
        info "adding lightdm.service drop-in (conflicts with getty@tty1)"
        as_root mkdir -p /etc/systemd/system/lightdm.service.d
        as_root tee /etc/systemd/system/lightdm.service.d/10-osr-vt1.conf >/dev/null <<'EOF'
# Written by os-rice (modules/lightdm.sh): the greeter is on vt1, so the getty
# that would otherwise print `login:` over it must give way.
[Unit]
Conflicts=getty@tty1.service
After=getty@tty1.service
EOF
        as_root systemctl daemon-reload >/dev/null 2>&1 || true
        # Enabled but never reached is the other half of this bug: with
        # multi-user.target as the default, nothing ever pulls in the DM.
        if [ "$(systemctl get-default 2>/dev/null)" != graphical.target ]; then
            info "setting the default systemd target to graphical.target"
            as_root systemctl set-default graphical.target >/dev/null 2>&1 \
                || warn "could not set graphical.target as default"
        fi ;;
    sysvinit)
        # tty1's getty is an /etc/inittab line here, and rewriting inittab is
        # machine territory. LightDM starting after it is cosmetic, not broken.
        warn "sysvinit: tty1's getty is set in /etc/inittab - comment its line out by hand if the console login still flashes before the greeter" ;;
esac

# --- greeter theme (§6b) ------------------------------------------------------
# The greeter needs the same GTK theme + icons as the session, or the login
# screen is stock grey while everything after it is themed.
_ld_src=$(osr_theme_source lightdm lightdm-gtk-greeter.conf) || _ld_src=""
if [ -n "$_ld_src" ]; then
    info "installing LightDM greeter theme"
    as_root mkdir -p /etc/lightdm
    if [ -f /etc/lightdm/lightdm-gtk-greeter.conf ] \
       && [ ! -f /etc/lightdm/lightdm-gtk-greeter.conf.bak ]; then
        as_root cp -f /etc/lightdm/lightdm-gtk-greeter.conf /etc/lightdm/lightdm-gtk-greeter.conf.bak
    fi
    # The greeter background needs its own copy under /usr/share, and this is
    # not tidiness. The greeter runs as `lightdm`, and a home directory is 0700
    # on Void, Debian and most others - so `lightdm` cannot even traverse into
    # ~/Pictures, let alone read the image. GTK does not report that: it draws
    # the fallback grey and says nothing, which reads as "the theme is broken"
    # rather than "one file is unreadable".
    #
    # So: install the wallpaper once for the user (§6, what the session uses),
    # then place a world-readable copy for the greeter and point the conf at it.
    _ld_wp=$(osr_install_wallpaper)
    if [ -n "$_ld_wp" ] && [ -f "$_ld_wp" ]; then
        _ld_wpdir=/usr/share/backgrounds/osr
        _ld_wpsys="$_ld_wpdir/$(basename "$_ld_wp")"
        as_root mkdir -p "$_ld_wpdir"
        as_root cp -f "$_ld_wp" "$_ld_wpsys"
        as_root chmod 0644 "$_ld_wpsys"
        info "greeter background: $_ld_wpsys"
        _ld_wp=$_ld_wpsys
    else
        warn "this theme ships no wallpaper - the greeter keeps its plain background"
    fi
    sed "s#{{WALLPAPER_PATH}}#${_ld_wp}#g" "$_ld_src" \
        | as_root tee /etc/lightdm/lightdm-gtk-greeter.conf >/dev/null
    case "$_ld_src" in "${TMPDIR:-/tmp}"/osr-theme-*) rm -f "$_ld_src" ;; esac
fi

# The .conf above only names a GTK theme; the rice palette itself reaches the
# greeter through GTK's user stylesheet, which GTK3 reads from the HOME of the
# user the greeter runs as (lightdm, /var/lib/lightdm). There is no `css=` key
# in lightdm-gtk-greeter.conf - this is the whole mechanism.
_ld_css=$(osr_theme_source lightdm gtk-greeter.css) || _ld_css=""
if [ -n "$_ld_css" ]; then
    _ld_user=lightdm
    _ld_home=$(getent passwd "$_ld_user" 2>/dev/null | cut -d: -f6)
    [ -n "$_ld_home" ] || _ld_home=/var/lib/lightdm
    if [ -d "$_ld_home" ]; then
        info "installing greeter CSS into $_ld_home/.config/gtk-3.0/gtk.css"
        as_root mkdir -p "$_ld_home/.config/gtk-3.0"
        as_root tee "$_ld_home/.config/gtk-3.0/gtk.css" <"$_ld_css" >/dev/null
        # GTK silently ignores a stylesheet it cannot read, and the greeter is
        # not root - the chown is what makes this file do anything.
        as_root chown -R "$_ld_user:$_ld_user" "$_ld_home/.config" 2>/dev/null \
            || warn "could not chown $_ld_home/.config to $_ld_user - greeter CSS may be ignored"
    else
        warn "no greeter home at $_ld_home - skipping greeter CSS"
    fi
    case "$_ld_css" in "${TMPDIR:-/tmp}"/osr-theme-*) rm -f "$_ld_css" ;; esac
fi

enable_service lightdm || warn "could not enable lightdm (needs a real init)"
