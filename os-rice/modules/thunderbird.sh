# session: x11+wayland
# modules/thunderbird.sh — mail/calendar. Same Mozilla profile machinery as
# modules/firefox.sh: a dotfiles-owned user.js and a rice-owned userChrome.css,
# installed into every profile under ~/.thunderbird (§5/§6).
#
# `evolution` is the packaged GTK alternative and `aerc`/`neomutt` the TUI ones
# (i3-sugg §9) — this module installs one mail client, not three.
#
# Note the profile root differs from Firefox's: ~/.thunderbird, not
# ~/.mozilla/thunderbird, on every current build.
#
# Debian/Ubuntu do not go through the archive: `thunderbird` resolves to
# source:provide_thunderbird_tarball (Mozilla's official Linux build) there,
# because the archive package is a snap stub on Ubuntu 24.04+ and an ESR too old
# for Exchange everywhere else. See lib/pkgmap/apt.map. Every other target keeps
# the native package: Fedora (152), RHEL/Alma/Rocky (140 ESR), Arch, Void and
# Alpine are all current enough for Exchange, so apt is the only special case.
#
# Exchange/Office 365: nothing to install. Thunderbird 140+ has an EWS backend
# built in, and the prefs that surface it live in dotfiles/thunderbird/user.js.
# Add the account with Account Setup -> Continue -> "Exchange"; Office 365 signs
# in through OAuth2 in a popup window. Mail only — Exchange *calendar* and
# address book still need an add-on (TbSync + its EWS provider), which is a
# per-user add-on install, not something a module can drop into a profile.

# De-snap first (apt only). This runs BEFORE pkg_install for a reason: the
# source: provider's idempotency probe is `command -v thunderbird` (§4), and a
# snap on PATH as /snap/bin/thunderbird would make the install skip itself — the
# snap would simply stay, profile in ~/snap and all. The transitional deb goes
# too: it owns a .desktop that re-launches the snap, and on a failed
# `snap install` its postinst leaves it half-installed, which plain --purge
# refuses — hence --force-all.
if [ "${OSR_PKG:-}" = apt ]; then
    if command -v snap >/dev/null 2>&1 && snap list thunderbird >/dev/null 2>&1; then
        info "removing the Thunderbird snap (its profile root is not ~/.thunderbird)"
        as_root snap remove --purge thunderbird || warn "snap remove thunderbird failed"
    fi
    if dpkg -s thunderbird 2>/dev/null | grep -q '^Version:.*snap'; then
        info "removing the archive's snap-transitional thunderbird package"
        as_root env DEBIAN_FRONTEND=noninteractive dpkg --purge --force-all thunderbird \
            || warn "could not purge the transitional thunderbird package"
    fi
fi

run_step "Installing Thunderbird" pkg_install thunderbird

# Exchange needs Thunderbird 140+. Every route this module takes should deliver
# it — Mozilla's tarball on apt, the native package on dnf/pacman/xbps/apk, all
# of which ship 140+ — so a lower version means this target pinned an old ESR.
# Say it here, once, instead of leaving someone hunting for an "Exchange" button
# that the account wizard is never going to draw.
_tb_ver=$(thunderbird --version 2>/dev/null | sed -n 's/^[^0-9]*\([0-9][0-9]*\).*/\1/p')
if [ -n "$_tb_ver" ] && [ "$_tb_ver" -lt 140 ]; then
    warn "Thunderbird $_tb_ver is older than 140 - Exchange/EWS accounts are unavailable; on x86_64, replacing it with Mozilla's build (provide_thunderbird_tarball, lib/build.sh) is the way out"
fi

_tb_root="$OSR_HOME/.thunderbird"
_tb_js=""
_tb_css=""
[ -f "$OSR_DOTFILES/thunderbird/user.js" ] && _tb_js="$OSR_DOTFILES/thunderbird/user.js"
_tb_css=$(osr_theme_source thunderbird userChrome.css) || _tb_css=""

if [ -n "$_tb_js" ] || [ -n "$_tb_css" ]; then
    install_mozilla_layer "$_tb_root" "$_tb_js" "$_tb_css"
fi
