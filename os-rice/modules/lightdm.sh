# session: x11
# modules/lightdm.sh — LightDM display manager + GTK greeter (i3-sugg §1.4).
# The lighter alternative to modules/sddm.sh for an X11 rice: no Qt, one config
# file, and it runs the PAM stack that unlocks the keyring at login (which
# `startx` does not).
#
# The greeter theme is rice-owned but lives in /etc (root), not ~/.config —
# the greeter runs as its own user before yours exists, so it cannot read your
# home. That is why this is the one theme layer written with as_root.

run_step "Installing LightDM" pkg_install lightdm lightdm-gtk-greeter

# The greeter needs the same GTK theme + icons as the session, or the login
# screen is stock grey while everything after it is themed.
if [ -n "${OSR_RICE_DIR:-}" ] && [ -f "$OSR_RICE_DIR/config/lightdm/lightdm-gtk-greeter.conf" ]; then
    info "installing LightDM greeter theme"
    as_root mkdir -p /etc/lightdm
    if [ -f /etc/lightdm/lightdm-gtk-greeter.conf ] \
       && [ ! -f /etc/lightdm/lightdm-gtk-greeter.conf.bak ]; then
        as_root cp -f /etc/lightdm/lightdm-gtk-greeter.conf /etc/lightdm/lightdm-gtk-greeter.conf.bak
    fi
    # The greeter background is the rice wallpaper, installed once by §6's
    # helper into the user's Pictures dir and readable by the greeter user.
    _ld_wp=$(osr_install_wallpaper)
    sed "s#{{WALLPAPER_PATH}}#${_ld_wp}#g" \
        "$OSR_RICE_DIR/config/lightdm/lightdm-gtk-greeter.conf" \
        | as_root tee /etc/lightdm/lightdm-gtk-greeter.conf >/dev/null
fi

enable_service lightdm || warn "could not enable lightdm (needs a real init)"
