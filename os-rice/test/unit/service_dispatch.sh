#!/bin/sh
# Proves §8 G3: enable_service emits the right command per OSR_INIT, using
# PATH-mocked init tools (no real services).
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB
NO_COLOR=1
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/service.sh"
. "$HERE/../lib.sh"

BIN=$(mktemp -d); OUT="$BIN/calls"
for t in systemctl rc-update rc-service ln update-rc.d service; do
    cat > "$BIN/$t" <<EOF
#!/bin/sh
[ "\$1" = "is-enabled" ] || [ "\$1" = "is-active" ] && exit 1
echo "$t \$*" >> "$OUT"
EOF
    chmod +x "$BIN/$t"
done
PATH="$BIN:$PATH"; export PATH
as_root() { "$@"; }

check() {  # <init> <expected substring>
    : > "$OUT"; OSR_INIT=$1; export OSR_INIT
    enable_service NetworkManager >/dev/null 2>&1 || true
    assert_contains "$OUT" "$2" "enable_service on $1"
}
check systemd  "systemctl enable --now NetworkManager"
check openrc   "rc-update add NetworkManager default"
check runit    "ln -s /etc/sv/NetworkManager /var/service/NetworkManager"
check sysvinit "update-rc.d NetworkManager enable"

rm -rf "$BIN"
finish
