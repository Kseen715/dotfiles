#!/usr/bin/env bash
# isp-route.sh — split egress routing on Debian.
#
# Goal: package managers / installers (apt, pip, cargo, npm, ...) go out over the
# ISP interface; everything else goes out over the cellular modem.
#
# Mechanism: a dedicated unix group ("ispnet") is used as a packet selector.
# nftables/iptables marks packets whose owning socket has that gid, and an
# `ip rule fwmark` sends those packets to a private routing table whose default
# route points at the ISP link. Nothing else on the box is affected.
#
#   sudo ./isp-route.sh setup       interactive install
#   sudo ./isp-route.sh status      show what is currently active
#   sudo ./isp-route.sh apply       re-apply from saved config (used at boot)
#   sudo ./isp-route.sh test        compare exit IP on both paths
#   sudo ./isp-route.sh teardown    remove everything
#
# After setup:  viaisp <any command>   forces that command onto the ISP link.

set -euo pipefail

VERSION=1.0.0
SELF=$(readlink -f "$0")

CONF=/etc/isp-route.conf
INSTALL_PATH=/usr/local/sbin/isp-route
WRAPPER=/usr/local/bin/viaisp
UNIT=/etc/systemd/system/isp-route.service
SYSCTL_FILE=/etc/sysctl.d/99-isp-route.conf
RT_DIR=/etc/iproute2/rt_tables.d
RT_FILE=$RT_DIR/isp-route.conf
PROFILE_FILE=/etc/profile.d/isp-route.sh
NM_DISPATCH=/etc/NetworkManager/dispatcher.d/90-isp-route

GROUP=ispnet
TABLE_ID=100
TABLE_NAME=ispnet
FWMARK=0x1
RULE_PRIO=1000
NFT_TABLE=isp_route
SHIM_TAG="isp-route-shim"

# Destinations never pushed onto the ISP table: they are on-link / RFC1918 and
# are reachable only through the main table (LAN, both routers, DNS on the
# router, mDNS, DHCP, ...).
EXEMPT4="127.0.0.0/8, 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16, 100.64.0.0/10, 169.254.0.0/16, 224.0.0.0/4, 255.255.255.255/32"

# Tools whose real binary lives in system dirs -> shadowed by a shim in
# /usr/local/bin (this also covers `sudo apt`, since sudo's secure_path has
# /usr/local/bin ahead of /usr/bin).
SYS_TOOLS="apt apt-get aptitude apt-file debootstrap pip pip3 pipx npm npx yarn pnpm cargo rustup go gem snap flatpak"
# Tools commonly installed per-user (~/.cargo/bin, ~/.local/bin, nvm, ...) that a
# /usr/local/bin shim cannot shadow -> shell functions instead.
FN_TOOLS="cargo rustup pip pip3 pipx npm npx yarn pnpm go gem uv poetry"

ASSUME_YES=0
OPT_ISP=""
OPT_MODEM=""

# ---------------------------------------------------------------- output ----

if [ -t 1 ]; then
	C_R=$'\e[31m'; C_G=$'\e[32m'; C_Y=$'\e[33m'; C_B=$'\e[36m'; C_D=$'\e[2m'; C_0=$'\e[0m'
else
	C_R=""; C_G=""; C_Y=""; C_B=""; C_D=""; C_0=""
fi

info() { printf '%s==>%s %s\n' "$C_B" "$C_0" "$*"; }
ok()   { printf '%s  ok%s %s\n' "$C_G" "$C_0" "$*"; }
warn() { printf '%swarn%s %s\n' "$C_Y" "$C_0" "$*" >&2; }
die()  { printf '%serr %s %s\n' "$C_R" "$C_0" "$*" >&2; exit 1; }
dim()  { printf '%s%s%s\n' "$C_D" "$*" "$C_0"; }

confirm() { # confirm <prompt> <default y|n>
	local prompt=$1 def=${2:-y} ans
	[ "$ASSUME_YES" = 1 ] && { echo "$prompt [auto: $def]"; [ "$def" = y ]; return; }
	if [ "$def" = y ]; then prompt="$prompt [Y/n] "; else prompt="$prompt [y/N] "; fi
	read -r -p "$prompt" ans </dev/tty || ans=""
	ans=${ans:-$def}
	case "$ans" in [yY]*) return 0 ;; *) return 1 ;; esac
}

ask() { # ask <prompt> <default>  -> echoes answer
	local prompt=$1 def=${2:-} ans
	[ "$ASSUME_YES" = 1 ] && { echo "$def"; return; }
	read -r -p "$prompt${def:+ [$def]}: " ans </dev/tty || ans=""
	echo "${ans:-$def}"
}

need_root() { [ "$(id -u)" -eq 0 ] || die "must run as root (use sudo)"; }

have() { command -v "$1" >/dev/null 2>&1; }

# ------------------------------------------------------------ discovery -----

iface_list() {
	ip -o link show | awk -F': ' '{print $2}' | cut -d'@' -f1 |
		grep -Ev '^(lo|docker[0-9]*|veth|br-|virbr|tun|tap|wg|zt)' || true
}

iface_addr()  { ip -o -4 addr show dev "$1" scope global 2>/dev/null | awk '{print $4}' | head -1 || true; }
iface_state() { cat "/sys/class/net/$1/operstate" 2>/dev/null || echo unknown; }

iface_gw() { # default gw currently known for this iface, empty for p2p links
	ip -4 route show default dev "$1" 2>/dev/null | sed -n 's/.*via \([0-9.]*\).*/\1/p' | head -1 || true
}

iface_kind() {
	local i=$1
	if [ -d "/sys/class/net/$i/wireless" ]; then echo wifi
	elif [ -d "/sys/class/net/$i/device/../.." ] && [[ $i == ww* || $i == ppp* || $i == usb* ]]; then echo modem
	elif [[ $i == en* || $i == eth* ]]; then echo ethernet
	else echo other; fi
}

print_iface_table() {
	printf '  %-3s %-14s %-9s %-20s %-16s %s\n' "#" "IFACE" "STATE" "ADDRESS" "GATEWAY" "TYPE"
	local n=0 i
	for i in "${IFACES[@]}"; do
		n=$((n + 1))
		printf '  %-3s %-14s %-9s %-20s %-16s %s\n' \
			"$n" "$i" "$(iface_state "$i")" "$(iface_addr "$i" || echo -)" \
			"$(iface_gw "$i" || echo -)" "$(iface_kind "$i")"
	done
}

pick_iface() { # pick_iface <role label> -> echoes iface name
	local label=$1 n choice
	while :; do
		choice=$(ask "Select the interface for $label (number or name)" "")
		[ -n "$choice" ] || continue
		if [[ $choice =~ ^[0-9]+$ ]]; then
			n=${#IFACES[@]}
			if [ "$choice" -ge 1 ] && [ "$choice" -le "$n" ]; then
				echo "${IFACES[$((choice - 1))]}"; return
			fi
		else
			for i in "${IFACES[@]}"; do
				[ "$i" = "$choice" ] && { echo "$i"; return; }
			done
		fi
		warn "no such interface: $choice"
	done
}

# --------------------------------------------------------------- config -----

load_conf() {
	[ -r "$CONF" ] || die "no config at $CONF — run '$SELF setup' first"
	# shellcheck disable=SC1090
	. "$CONF"
	: "${ISP_IF:?}" "${MODEM_IF:?}"
	ISP_GW_STATIC=${ISP_GW_STATIC:-}
	MODEM_GW_STATIC=${MODEM_GW_STATIC:-}
	PREFER_MODEM_DEFAULT=${PREFER_MODEM_DEFAULT:-yes}
	BLOCK_IPV6=${BLOCK_IPV6:-yes}
	DNS_SERVER=${DNS_SERVER:-}
	SHIMS=${SHIMS:-}
}

save_conf() {
	umask 022
	cat >"$CONF" <<EOF
# generated by isp-route $VERSION on $(date -Is)
ISP_IF=$ISP_IF
MODEM_IF=$MODEM_IF
ISP_GW_STATIC=$ISP_GW_STATIC
MODEM_GW_STATIC=$MODEM_GW_STATIC
PREFER_MODEM_DEFAULT=$PREFER_MODEM_DEFAULT
BLOCK_IPV6=$BLOCK_IPV6
DNS_SERVER=$DNS_SERVER
SHIMS="$SHIMS"
EOF
	ok "wrote $CONF"
}

# ------------------------------------------------------ network plumbing -----

group_gid() { getent group "$GROUP" 2>/dev/null | cut -d: -f3 || true; }

ensure_group() {
	getent group "$GROUP" >/dev/null || { groupadd --system "$GROUP"; ok "created group $GROUP"; }
	GID=$(group_gid)
	[ -n "$GID" ] || die "could not resolve gid for group $GROUP"
}

ensure_rt_table() {
	mkdir -p "$RT_DIR"
	printf '%s\t%s\n' "$TABLE_ID" "$TABLE_NAME" >"$RT_FILE"
}

apply_sysctl() {
	cat >"$SYSCTL_FILE" <<EOF
# isp-route: loose reverse-path filtering, required for multi-homed policy routing
net.ipv4.conf.all.rp_filter = 2
net.ipv4.conf.default.rp_filter = 2
net.ipv4.conf.$ISP_IF.rp_filter = 2
net.ipv4.conf.$MODEM_IF.rp_filter = 2
EOF
	sysctl -q --system >/dev/null 2>&1 || sysctl -q -p "$SYSCTL_FILE" >/dev/null 2>&1 || true
}
apply_routes() {
	local isp_addr isp_gw modem_gw net

	isp_addr=$(iface_addr "$ISP_IF"); isp_addr=${isp_addr%%/*}
	[ -n "$isp_addr" ] || die "$ISP_IF has no IPv4 address — is it up?"

	isp_gw=${ISP_GW_STATIC:-$(iface_gw "$ISP_IF")}
	modem_gw=${MODEM_GW_STATIC:-$(iface_gw "$MODEM_IF")}

	# private table: on-link nets first, then the default via the ISP router
	ip -4 route flush table "$TABLE_ID" 2>/dev/null || true
	for net in $(ip -4 route show dev "$ISP_IF" scope link 2>/dev/null | awk '{print $1}'); do
		ip -4 route replace "$net" dev "$ISP_IF" src "$isp_addr" table "$TABLE_ID"
	done
	if [ -n "$isp_gw" ]; then
		ip -4 route replace default via "$isp_gw" dev "$ISP_IF" src "$isp_addr" onlink table "$TABLE_ID"
	else
		ip -4 route replace default dev "$ISP_IF" src "$isp_addr" table "$TABLE_ID"
	fi
	ok "table $TABLE_NAME: default via ${isp_gw:-$ISP_IF} dev $ISP_IF src $isp_addr"

	# the fwmark rule that feeds it
	while ip -4 rule show | grep -q "fwmark $FWMARK"; do
		ip -4 rule del fwmark "$FWMARK" 2>/dev/null || break
	done
	ip -4 rule add fwmark "$FWMARK" lookup "$TABLE_ID" priority "$RULE_PRIO"
	ok "ip rule: fwmark $FWMARK -> table $TABLE_NAME (prio $RULE_PRIO)"

	# main table: everything unmarked prefers the modem
	if [ "$PREFER_MODEM_DEFAULT" = yes ]; then
		while ip -4 route show default dev "$MODEM_IF" | grep -q .; do
			ip -4 route del default dev "$MODEM_IF" 2>/dev/null || break
		done
		while ip -4 route show default dev "$ISP_IF" | grep -q .; do
			ip -4 route del default dev "$ISP_IF" 2>/dev/null || break
		done
		if [ -n "$modem_gw" ]; then
			ip -4 route add default via "$modem_gw" dev "$MODEM_IF" metric 50 onlink
		else
			ip -4 route add default dev "$MODEM_IF" metric 50
		fi
		if [ -n "$isp_gw" ]; then
			ip -4 route add default via "$isp_gw" dev "$ISP_IF" metric 600 onlink
		else
			ip -4 route add default dev "$ISP_IF" metric 600
		fi
		ok "main table: default via ${modem_gw:-$MODEM_IF} dev $MODEM_IF (metric 50), $ISP_IF demoted to 600"
	fi
}

apply_nft() {
	local dns_rules="" nat_chain="" v6chain="" tmp
	if [ -n "$DNS_SERVER" ]; then
		# Must come BEFORE the gid test: lookups are not made by the calling
		# process, they are made by the system resolver daemon under its own uid,
		# so an owner match can never see them. Loopback is excluded above so the
		# resolver's own stub listener is not hijacked.
		dns_rules="		meta l4proto { tcp, udp } th dport 53 meta mark set $FWMARK
		meta mark $FWMARK return"
		nat_chain="
	chain outnat {
		type nat hook output priority dstnat; policy accept;
		meta mark $FWMARK meta l4proto { tcp, udp } th dport 53 dnat ip to $DNS_SERVER:53
	}"
	fi
	if [ "$BLOCK_IPV6" = yes ]; then
		v6chain="
	chain output6 {
		type filter hook output priority filter; policy accept;
		meta nfproto ipv6 meta skgid $GID ip6 daddr != { ::1/128, fe80::/10, fc00::/7 } reject
	}"
	fi

	tmp=$(mktemp)
	cat >"$tmp" <<EOF
table inet $NFT_TABLE
delete table inet $NFT_TABLE
table inet $NFT_TABLE {
	set exempt4 {
		type ipv4_addr
		flags interval
		elements = { $EXEMPT4 }
	}

	chain output {
		type route hook output priority mangle; policy accept;
		ip daddr 127.0.0.0/8 return
${dns_rules}
		meta skgid != $GID return
		ip daddr @exempt4 return
		meta mark set $FWMARK
	}

	chain postrouting {
		type nat hook postrouting priority srcnat; policy accept;
		meta mark $FWMARK oifname "$ISP_IF" masquerade
	}${nat_chain}${v6chain}
}
EOF
	nft -f "$tmp" || { cp "$tmp" /tmp/isp-route.nft; die "nft failed; ruleset kept at /tmp/isp-route.nft"; }
	rm -f "$tmp"
	ok "nftables: table inet $NFT_TABLE (gid $GID -> mark $FWMARK)"
}

apply_iptables() {
	local net
	iptables -t mangle -N ISP_ROUTE 2>/dev/null || iptables -t mangle -F ISP_ROUTE
	iptables -t mangle -C OUTPUT -j ISP_ROUTE 2>/dev/null || iptables -t mangle -A OUTPUT -j ISP_ROUTE
	# loopback first, so the resolver's own 127.0.0.53 stub is never touched
	iptables -t mangle -A ISP_ROUTE -d 127.0.0.0/8 -j RETURN
	if [ -n "$DNS_SERVER" ]; then
		# before the owner test on purpose — see apply_nft()
		iptables -t mangle -A ISP_ROUTE -p udp --dport 53 -j MARK --set-mark "$FWMARK"
		iptables -t mangle -A ISP_ROUTE -p tcp --dport 53 -j MARK --set-mark "$FWMARK"
		iptables -t mangle -A ISP_ROUTE -m mark --mark "$FWMARK" -j RETURN
		iptables -t nat -C OUTPUT -m mark --mark "$FWMARK" -p udp --dport 53 -j DNAT --to-destination "$DNS_SERVER:53" 2>/dev/null ||
			iptables -t nat -A OUTPUT -m mark --mark "$FWMARK" -p udp --dport 53 -j DNAT --to-destination "$DNS_SERVER:53"
		iptables -t nat -C OUTPUT -m mark --mark "$FWMARK" -p tcp --dport 53 -j DNAT --to-destination "$DNS_SERVER:53" 2>/dev/null ||
			iptables -t nat -A OUTPUT -m mark --mark "$FWMARK" -p tcp --dport 53 -j DNAT --to-destination "$DNS_SERVER:53"
	fi
	iptables -t mangle -A ISP_ROUTE -m owner ! --gid-owner "$GID" -j RETURN
	for net in ${EXEMPT4//,/ }; do
		iptables -t mangle -A ISP_ROUTE -d "$net" -j RETURN
	done
	iptables -t mangle -A ISP_ROUTE -j MARK --set-mark "$FWMARK"
	iptables -t nat -C POSTROUTING -m mark --mark "$FWMARK" -o "$ISP_IF" -j MASQUERADE 2>/dev/null ||
		iptables -t nat -A POSTROUTING -m mark --mark "$FWMARK" -o "$ISP_IF" -j MASQUERADE
	if [ "$BLOCK_IPV6" = yes ] && have ip6tables; then
		ip6tables -C OUTPUT -m owner --gid-owner "$GID" ! -d ::1/128 -j REJECT 2>/dev/null ||
			ip6tables -A OUTPUT -m owner --gid-owner "$GID" ! -d ::1/128 -j REJECT
	fi
	ok "iptables: mangle/ISP_ROUTE (gid $GID -> mark $FWMARK)"
}

apply_firewall() {
	if have nft; then apply_nft
	elif have iptables; then apply_iptables
	else die "neither nft nor iptables found — install nftables"; fi
}

flush_firewall() {
	have nft && nft delete table inet "$NFT_TABLE" 2>/dev/null || true
	if have iptables; then
		iptables -t mangle -D OUTPUT -j ISP_ROUTE 2>/dev/null || true
		iptables -t mangle -F ISP_ROUTE 2>/dev/null || true
		iptables -t mangle -X ISP_ROUTE 2>/dev/null || true
		while iptables -t nat -D POSTROUTING -m mark --mark "$FWMARK" -o "${ISP_IF:-eth0}" -j MASQUERADE 2>/dev/null; do :; done
		if [ -n "${DNS_SERVER:-}" ]; then
			local p
			for p in udp tcp; do
				while iptables -t nat -D OUTPUT -m mark --mark "$FWMARK" -p "$p" --dport 53 \
					-j DNAT --to-destination "$DNS_SERVER:53" 2>/dev/null; do :; done
			done
		fi
	fi
	have ip6tables && { while ip6tables -D OUTPUT -m owner --gid-owner "$(group_gid)" ! -d ::1/128 -j REJECT 2>/dev/null; do :; done; } || true
}

# -------------------------------------------------------------- wrapper -----

install_wrapper() {
	umask 022
	{
		echo '#!/usr/bin/env bash'
		echo "# generated by isp-route $VERSION — run a command over the ISP link"
		echo "GROUP=$GROUP"
		cat <<'EOF'
set -euo pipefail
if [ $# -eq 0 ]; then
	echo "usage: viaisp <command> [args...]" >&2
	exit 2
fi
GID=$(getent group "$GROUP" 2>/dev/null | cut -d: -f3) || GID=""
[ -n "$GID" ] || { echo "viaisp: group '$GROUP' missing — run 'sudo isp-route setup'" >&2; exit 1; }
if [ "$(id -u)" -eq 0 ]; then
	exec setpriv --regid "$GID" --clear-groups -- "$@"
fi
me=$(id -un)
if ! getent group "$GROUP" | cut -d: -f4 | tr ',' '\n' | grep -qx "$me"; then
	echo "viaisp: $me is not a member of group '$GROUP'" >&2
	echo "        fix: sudo usermod -aG $GROUP $me   (then log out and back in)" >&2
	exit 1
fi
cmd=$(printf '%q ' "$@")
exec sg "$GROUP" -c "$cmd"
EOF
	} >"$WRAPPER"
	chmod 0755 "$WRAPPER"
	ok "installed $WRAPPER"
}

real_binary() { # resolve a tool outside /usr/local so shims never recurse
	PATH=/usr/bin:/bin:/usr/sbin:/sbin command -v "$1" 2>/dev/null || true
}

install_shims() {
	local tool real path installed=""
	for tool in $SYS_TOOLS; do
		real=$(real_binary "$tool")
		[ -n "$real" ] || continue
		path=/usr/local/bin/$tool
		if [ -e "$path" ] && ! grep -q "$SHIM_TAG" "$path" 2>/dev/null; then
			warn "skipping shim for $tool: $path already exists and is not ours"
			continue
		fi
		cat >"$path" <<EOF
#!/bin/sh
# $SHIM_TAG — route $tool through the ISP link
exec $WRAPPER $real "\$@"
EOF
		chmod 0755 "$path"
		installed="$installed $tool"
	done
	SHIMS=${installed# }
	[ -n "$SHIMS" ] && ok "shims in /usr/local/bin:$installed" || warn "no system tools found to shim"
}

remove_shims() {
	local tool path
	for tool in $SYS_TOOLS ${SHIMS:-}; do
		path=/usr/local/bin/$tool
		if [ -f "$path" ] && grep -q "$SHIM_TAG" "$path" 2>/dev/null; then
			rm -f "$path"
		fi
	done
}

install_functions() {
	umask 022
	{
		echo "# generated by isp-route $VERSION"
		echo "# per-user installers (~/.cargo/bin, ~/.local/bin, nvm, venvs) cannot be"
		echo "# shadowed from /usr/local/bin, so wrap them as shell functions instead."
		echo "if [ -x $WRAPPER ]; then"
		echo "  for _isp_t in $FN_TOOLS; do"
		echo '    _isp_p=$(command -v "$_isp_t" 2>/dev/null) || continue'
		echo '    [ -n "$_isp_p" ] || continue'
		echo "    grep -qs '$SHIM_TAG' \"\$_isp_p\" && continue  # already shimmed"
		echo "    eval \"\${_isp_t}() { $WRAPPER \${_isp_t} \\\"\\\$@\\\"; }\""
		echo '  done'
		echo '  unset _isp_t _isp_p'
		echo 'fi'
	} >"$PROFILE_FILE"
	chmod 0644 "$PROFILE_FILE"
	ok "installed $PROFILE_FILE (shell functions for per-user tools)"
}

install_service() {
	install -m 0755 "$SELF" "$INSTALL_PATH"
	cat >"$UNIT" <<EOF
[Unit]
Description=isp-route policy routing (installers over ISP link)
After=network-online.target
Wants=network-online.target

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=$INSTALL_PATH apply

[Install]
WantedBy=multi-user.target
EOF
	systemctl daemon-reload
	systemctl enable isp-route.service >/dev/null 2>&1 || warn "could not enable isp-route.service"
	ok "installed $INSTALL_PATH + isp-route.service (re-applies on boot)"

	if [ -d "$(dirname "$NM_DISPATCH")" ]; then
		cat >"$NM_DISPATCH" <<EOF
#!/bin/sh
# generated by isp-route — re-apply after NetworkManager reconfigures a link
case "\$2" in
	up|dhcp4-change|connectivity-change) $INSTALL_PATH apply >/dev/null 2>&1 || true ;;
esac
EOF
		chmod 0755 "$NM_DISPATCH"
		ok "installed NetworkManager dispatcher hook"
	fi
}

# ------------------------------------------------------------ subcommands ----

cmd_setup() {
	need_root
	have ip || die "iproute2 not installed"
	have nft || have iptables || die "install nftables (apt install nftables)"
	have sg || warn "'sg' not found (package: passwd) — non-root viaisp will not work"

	mapfile -t IFACES < <(iface_list)
	[ "${#IFACES[@]}" -gt 0 ] || die "no usable interfaces found"

	info "Detected interfaces"
	print_iface_table
	echo

	if [ -n "$OPT_ISP" ]; then ISP_IF=$OPT_ISP; else ISP_IF=$(pick_iface "your ${C_G}ISP / full-access${C_0} link"); fi
	if [ -n "$OPT_MODEM" ]; then MODEM_IF=$OPT_MODEM; else MODEM_IF=$(pick_iface "your ${C_Y}cellular / restricted${C_0} link"); fi
	[ "$ISP_IF" != "$MODEM_IF" ] || die "ISP and modem interface must differ"

	ISP_GW_STATIC=$(iface_gw "$ISP_IF")
	if [ -z "$ISP_GW_STATIC" ]; then
		if [ "$(iface_kind "$ISP_IF")" != modem ]; then
			warn "no default gateway detected on $ISP_IF"
			ISP_GW_STATIC=$(ask "Gateway IP for $ISP_IF (empty = point-to-point link)" "")
		fi
	else
		dim "  detected gateway on $ISP_IF: $ISP_GW_STATIC"
		ISP_GW_STATIC=$(ask "Gateway for $ISP_IF" "$ISP_GW_STATIC")
	fi

	MODEM_GW_STATIC=$(iface_gw "$MODEM_IF")
	[ -n "$MODEM_GW_STATIC" ] && dim "  detected gateway on $MODEM_IF: $MODEM_GW_STATIC"

	echo
	if confirm "Make $MODEM_IF the default route for all non-installer traffic?" y; then
		PREFER_MODEM_DEFAULT=yes
	else
		PREFER_MODEM_DEFAULT=no
	fi

	if confirm "Block IPv6 for installer traffic? (keeps it from leaking around the v4 policy)" y; then
		BLOCK_IPV6=yes
	else
		BLOCK_IPV6=no
	fi

	echo
	dim "  Lookups are made by the system resolver daemon, not by apt/pip, so they"
	dim "  cannot be split per-process. If your DHCP resolver poisons or blocks names,"
	dim "  give a server here: ALL port-53 traffic is then redirected to it over the"
	dim "  ISP link, transparently. Your resolv.conf / resolved config is not touched."
	dim "  Leave empty to keep DNS exactly as it is today."
	DNS_SERVER=$(ask "Redirect all DNS to this server over the ISP link (e.g. 9.9.9.9, empty = no change)" "")

	local do_shims=no do_fns=no do_service=no add_user=""
	confirm "Install shims so 'apt', 'pip', 'npm', ... automatically use the ISP link?" y && do_shims=yes
	confirm "Install /etc/profile.d hooks for per-user tools (cargo, pipx, nvm)?" y && do_fns=yes
	confirm "Install a systemd unit so this survives reboots and DHCP renewals?" y && do_service=yes

	local defuser=${SUDO_USER:-}
	if [ -n "$defuser" ]; then
		confirm "Add user '$defuser' to group '$GROUP' (needed to use viaisp without sudo)?" y && add_user=$defuser
	fi

	echo
	info "Summary"
	printf '  installers  -> %s%s%s%s\n' "$C_G" "$ISP_IF" "$C_0" "${ISP_GW_STATIC:+ via $ISP_GW_STATIC}"
	printf '  everything  -> %s%s%s%s\n' "$C_Y" "$MODEM_IF" "$C_0" "${MODEM_GW_STATIC:+ via $MODEM_GW_STATIC}"
	printf '  selector    -> unix group %s, fwmark %s, table %s (%s)\n' "$GROUP" "$FWMARK" "$TABLE_NAME" "$TABLE_ID"
	printf '  ipv6 block  -> %s\n' "$BLOCK_IPV6"
	printf '  dns         -> %s\n' "${DNS_SERVER:-system default}"
	echo
	confirm "Apply this configuration?" y || die "aborted"

	ensure_group
	ensure_rt_table
	apply_sysctl
	apply_routes
	apply_firewall
	install_wrapper
	SHIMS=""
	[ "$do_shims" = yes ] && install_shims
	[ "$do_fns" = yes ] && install_functions
	save_conf
	[ "$do_service" = yes ] && install_service
	if [ -n "$add_user" ]; then
		usermod -aG "$GROUP" "$add_user"
		ok "added $add_user to $GROUP (log out and back in for it to take effect)"
	fi

	echo
	info "Done. Usage:"
	dim "  viaisp curl https://example.com     # force anything onto the ISP link"
	dim "  sudo apt update                     # already routed via $ISP_IF (shim)"
	dim "  sudo isp-route status               # inspect"
	dim "  sudo isp-route test                 # compare exit IPs"
	dim "  sudo isp-route teardown             # undo everything"
}

cmd_apply() {
	need_root
	load_conf
	ensure_group
	ensure_rt_table
	apply_sysctl
	apply_routes
	apply_firewall
	[ -x "$WRAPPER" ] || install_wrapper
}

cmd_status() {
	if [ -r "$CONF" ]; then
		load_conf
		info "Config ($CONF)"
		printf '  ISP link   : %s%s\n' "$ISP_IF" "${ISP_GW_STATIC:+ via $ISP_GW_STATIC}"
		printf '  Modem link : %s%s\n' "$MODEM_IF" "${MODEM_GW_STATIC:+ via $MODEM_GW_STATIC}"
		printf '  Group      : %s (gid %s)\n' "$GROUP" "$(g=$(group_gid); echo "${g:-missing}")"
		printf '  Shims      : %s\n' "${SHIMS:-none}"
	else
		warn "not configured ($CONF missing)"
	fi
	echo; info "ip rules";        ip -4 rule show | sed 's/^/  /'
	echo; info "table $TABLE_NAME"; ip -4 route show table "$TABLE_ID" 2>/dev/null | sed 's/^/  /' || echo "  (empty)"
	echo; info "main default";    ip -4 route show default | sed 's/^/  /'
	if have nft && nft list table inet "$NFT_TABLE" >/dev/null 2>&1; then
		echo; info "nftables"; nft list table inet "$NFT_TABLE" | sed 's/^/  /'
	elif have iptables && iptables -t mangle -S ISP_ROUTE >/dev/null 2>&1; then
		echo; info "iptables"; iptables -t mangle -S ISP_ROUTE | sed 's/^/  /'
	else
		echo; warn "no packet-marking rules active"
	fi
}

cmd_test() {
	load_conf
	have curl || die "curl not installed"
	local url=https://api.ipify.org a b
	info "Probing $url on both paths (8s timeout each)"
	a=$(curl -4 -s --max-time 8 "$url" || echo "FAILED")
	b=$("$WRAPPER" curl -4 -s --max-time 8 "$url" || echo "FAILED")
	printf '  default path (%s) : %s\n' "$MODEM_IF" "$a"
	printf '  viaisp  path (%s) : %s\n' "$ISP_IF" "$b"
	if [ "$a" != "$b" ] && [ "$b" != FAILED ]; then
		ok "split routing is working (different exit IPs)"
	elif [ "$b" = FAILED ]; then
		warn "ISP path failed — check 'isp-route status' and the ISP gateway"
	else
		warn "both paths report the same IP — marking may not be taking effect"
	fi
}

cmd_teardown() {
	need_root
	[ -r "$CONF" ] && load_conf || { ISP_IF=""; MODEM_IF=""; SHIMS=""; }
	confirm "Remove all isp-route configuration?" y || die "aborted"

	flush_firewall
	while ip -4 rule show | grep -q "fwmark $FWMARK"; do
		ip -4 rule del fwmark "$FWMARK" 2>/dev/null || break
	done
	ip -4 route flush table "$TABLE_ID" 2>/dev/null || true
	remove_shims
	rm -f "$WRAPPER" "$PROFILE_FILE" "$SYSCTL_FILE" "$RT_FILE" "$NM_DISPATCH"
	if [ -f "$UNIT" ]; then
		systemctl disable --now isp-route.service >/dev/null 2>&1 || true
		rm -f "$UNIT"
		systemctl daemon-reload
	fi
	rm -f "$INSTALL_PATH" "$CONF"
	sysctl -q --system >/dev/null 2>&1 || true
	ok "removed. The group '$GROUP' was left in place (groupdel $GROUP to drop it)."
	warn "default routes were left as-is; restart NetworkManager/networkd to regenerate them"
}

usage() {
	cat <<EOF
isp-route $VERSION — send installers over the ISP link, everything else over the modem

  isp-route setup [--isp IF] [--modem IF] [--yes]   interactive install
  isp-route apply                                   re-apply saved config
  isp-route status                                  show active configuration
  isp-route test                                    compare exit IP on both paths
  isp-route teardown                                remove everything
  isp-route ifaces                                  list candidate interfaces

After setup, 'viaisp <cmd>' forces any command onto the ISP link.
EOF
}

main() {
	local cmd=${1:-setup}
	[ $# -gt 0 ] && shift || true
	while [ $# -gt 0 ]; do
		case "$1" in
			--yes | -y) ASSUME_YES=1 ;;
			--isp) OPT_ISP=${2:-}; shift ;;
			--modem) OPT_MODEM=${2:-}; shift ;;
			*) die "unknown option: $1" ;;
		esac
		shift
	done
	case "$cmd" in
		setup | install) cmd_setup ;;
		apply | up) cmd_apply ;;
		status | show) cmd_status ;;
		test | check) cmd_test ;;
		teardown | down | uninstall) cmd_teardown ;;
		ifaces | list) mapfile -t IFACES < <(iface_list); print_iface_table ;;
		-h | --help | help) usage ;;
		*) usage; exit 1 ;;
	esac
}

main "$@"
