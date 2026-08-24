# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/avahi.sh — mDNS/zeroconf (i3-sugg §7.1). This is what makes
# `.local` hostnames resolve, driverless network printers appear in CUPS, and
# KDE Connect / LocalSend find peers at all.
#
# Two halves: the daemon (avahi) and the NSS plugin (nss-mdns). Installing the
# daemon alone is the classic half-configuration — the printer shows up in the
# CUPS web UI but nothing can resolve its name, because /etc/nsswitch.conf still
# has no `mdns` entry. The edit below is idempotent and only touches the hosts:
# line (§2).

run_step "Installing Avahi (mDNS)" pkg_install avahi nss-mdns

if [ -f /etc/nsswitch.conf ]; then
    if grep -E '^hosts:' /etc/nsswitch.conf | grep -q mdns; then
        info "/etc/nsswitch.conf already resolves mdns - skipping"
    else
        info "adding mdns4_minimal to the hosts: line in /etc/nsswitch.conf"
        as_root cp -f /etc/nsswitch.conf /etc/nsswitch.conf.bak
        # Standard upstream ordering: mdns before dns, with [NOTFOUND=return] so
        # a negative mDNS answer does not stall every lookup.
        sed -E 's/^(hosts:[[:space:]]*)(.*)$/\1mdns4_minimal [NOTFOUND=return] \2/' \
            /etc/nsswitch.conf | as_root tee /etc/nsswitch.conf.new >/dev/null
        as_root mv /etc/nsswitch.conf.new /etc/nsswitch.conf
    fi
fi

enable_service avahi-daemon || warn "could not enable avahi-daemon (needs a real init)"
