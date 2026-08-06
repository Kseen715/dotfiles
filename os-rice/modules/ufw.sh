# session: x11+wayland
# modules/ufw.sh — host firewall (i3-sugg §7.1). ufw over raw nftables rules
# because the rules a desktop needs are three lines, and gufw gives you a GUI
# for the fourth.
#
# Policy is deliberately conservative and set only on a fresh install: deny
# inbound, allow outbound. Rewriting an existing ruleset on every rerun would
# silently undo whatever the machine's owner opened (§2 — never override user
# state). The firewall is enabled, but that is the only mutation on a rerun.
#
# Note: this closes mDNS (5353/udp) and KDE Connect (1714-1764) by default. The
# commented lines below are the two most people want back.

run_step "Installing ufw" pkg_install ufw gufw

if command -v ufw >/dev/null 2>&1; then
    if as_root ufw status 2>/dev/null | grep -q 'Status: active'; then
        info "ufw already active - leaving the existing ruleset alone"
    else
        info "setting the default ufw policy (deny in, allow out)"
        as_root ufw --force default deny incoming  || warn "ufw default deny incoming failed"
        as_root ufw --force default allow outgoing || warn "ufw default allow outgoing failed"
        # Uncomment in a fork of this module if the desktop needs them:
        # as_root ufw allow 5353/udp comment 'mDNS'
        # as_root ufw allow 1714:1764/udp comment 'KDE Connect'
        # as_root ufw allow 1714:1764/tcp comment 'KDE Connect'
        as_root ufw --force enable || warn "could not enable ufw"
    fi
fi

enable_service ufw || warn "could not enable the ufw service (needs a real init)"
