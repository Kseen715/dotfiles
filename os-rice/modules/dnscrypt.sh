# session: x11+wayland
# modules/dnscrypt.sh — encrypted DNS (i3-sugg §7.1). Available module, NOT in
# the default rice: swapping the resolver is a decision with real failure modes
# (captive portals, split-horizon corporate DNS, VPN-pushed resolvers), so it
# should be opted into, not inherited.
#
# The service is installed and enabled, but NetworkManager is deliberately left
# pointing at whatever it already uses. Wiring it up is one line, and it belongs
# to the machine, not to a rice:
#
#   /etc/NetworkManager/conf.d/00-dnscrypt.conf
#     [main]
#     dns=none
#   /etc/resolv.conf  ->  nameserver 127.0.0.1
#
# openresolv is what keeps a VPN from stomping that file.

run_step "Installing dnscrypt-proxy" pkg_install dnscrypt-proxy openresolv

enable_service dnscrypt-proxy || warn "could not enable dnscrypt-proxy (needs a real init)"
info "dnscrypt-proxy installed but NOT wired into NetworkManager - see the header of this module"
