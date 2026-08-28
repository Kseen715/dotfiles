#!/bin/sh
# Proves lib/service.c drives an init exactly as lib/service.sh did: the same
# unit name resolved out of servicemap, the same commands in the same order on
# each of the four inits, and the same idempotence skips.
#
# Hermetic like test/unit/git_c_parity.sh: PATH is reduced to a stub bin/, so
# systemctl, rc-update, service and sudo are all scenario-controlled and no
# real init is ever touched.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip service_c_parity: %s is not built\n' "$OSR_BIN"
    exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
BIN="$TMP/bin"; mkdir -p "$BIN"
REAL_SH=$(command -v sh)

for _t in env cat cut grep sed awk tr head tail printf id mktemp rm cp mv mkdir \
          ln ls find sort wc dirname basename test true false; do
    _p=$(command -v "$_t" 2>/dev/null) || :
    case "$_p" in
        /*) ln -sf "$_p" "$BIN/$_t" ;;
        *)  for _d in /usr/bin /bin /usr/local/bin; do
                [ -x "$_d/$_t" ] && { ln -sf "$_d/$_t" "$BIN/$_t"; break; }
            done ;;
    esac
done
ln -sf "$REAL_SH" "$BIN/sh"

# Every init tool logs its argv and answers from the scenario's marker files:
# ENABLED / ACTIVE say what systemctl should report.
for _tool in systemctl rc-update rc-service update-rc.d service; do
    cat >"$BIN/$_tool" <<EOF
#!/bin/sh
printf '$_tool %s\n' "\$*" >>"\$LOG"
case "\$1" in
    is-enabled) [ -f "\$STATE/ENABLED" ] || exit 1 ;;
    is-active)  [ -f "\$STATE/ACTIVE" ]  || exit 1 ;;
esac
exit 0
EOF
    chmod +x "$BIN/$_tool"
done

# sudo: logged, then the real command, so as_root's escalation shows up in the
# log without the test needing any privilege.
cat >"$BIN/sudo" <<'EOF'
#!/bin/sh
printf 'sudo %s\n' "$*" >>"$LOG"
exec "$@"
EOF
chmod +x "$BIN/sudo"

FACTS="OSR_ROOT=$OSR_ROOT OSR_LIB=$OSR_LIB OSR_DISTRO=void OSR_ARCH=x86_64
       NO_COLOR=1 TERM=dumb OSR_PKG=xbps"
ME=$(id -un)

# seed <root> -- redefined per scenario; lays out one sandbox. $ROOT is bound.
seed() { :; }
INIT=systemd

run_side() {
    _root=$1; _tier=$2; _cmd=$3
    rm -rf "$_root"; mkdir -p "$_root/state" "$_root/sv" "$_root/service"
    ROOT=$_root; seed
    : >"$_root/log"
    if [ "$_tier" = sh ]; then
        # shellcheck disable=SC2086
        env -i PATH="$BIN" LOG="$_root/log" STATE="$_root/state" $FACTS \
            OSR_INIT="$INIT" OSR_USER="$ME" OSR_HOME="$_root" HOME="$_root" \
            OSR_SV_DIR="$_root/sv" OSR_SERVICE_DIR="$_root/service" ROOT="$_root" \
            sh -c '
                . "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/user.sh"
                . "$OSR_LIB/service.sh"
                eval "$1"' _ "$_cmd" 2>&1 || :
    else
        # shellcheck disable=SC2086
        env -i PATH="$BIN" LOG="$_root/log" STATE="$_root/state" $FACTS \
            OSR_INIT="$INIT" OSR_USER="$ME" OSR_HOME="$_root" HOME="$_root" \
            OSR_SV_DIR="$_root/sv" OSR_SERVICE_DIR="$_root/service" ROOT="$_root" \
            "$OSR_BIN" service $_cmd 2>&1 || :
    fi
}

# dump_links <root> -- what the runit branch leaves in the service dir, link
# targets included, with the sandbox path collapsed so two roots compare equal.
dump_links() {
    (cd "$1" && find service sv | sort | while read -r _p; do
        if [ -L "$_p" ]; then printf '%s -> %s\n' "$_p" "$(ls -ld "$_p" | sed 's/.*-> //')"
        else printf '%s\n' "$_p"; fi
    done) | sed "s|$1|ROOT|g"
}

# scene <label> <sh-snippet> <c-args> -- both tiers, everything compared.
scene() {
    _label=$1
    _sh_out=$(run_side "$TMP/a" sh "$2" | sed "s|$TMP/a|ROOT|g")
    _c_out=$(run_side "$TMP/b" c "$3" | sed "s|$TMP/b|ROOT|g")
    assert_eq "$_sh_out" "$_c_out" "$_label: same output"
    assert_eq "$(sed "s|$TMP/a|ROOT|g" <"$TMP/a/log")" \
              "$(sed "s|$TMP/b|ROOT|g" <"$TMP/b/log")" "$_label: same commands"
    assert_eq "$(dump_links "$TMP/a")" "$(dump_links "$TMP/b")" "$_label: same links"
}

# --- 1. servicemap resolution -------------------------------------------------
# The @init-qualified row wins for that init; every other init falls through to
# the logical name unchanged.
INIT=runit
scene "runit maps bluetooth to bluetoothd" 'service_resolve bluetooth' 'resolve bluetooth'
scene "runit maps cups to cupsd"           'service_resolve cups'      'resolve cups'
scene "runit maps smb to smbd"             'service_resolve smb'       'resolve smb'
scene "runit leaves an unlisted name"      'service_resolve sshd'      'resolve sshd'
INIT=systemd
scene "systemd ignores the runit rows"     'service_resolve bluetooth' 'resolve bluetooth'
scene "a comment row is not a service"     'service_resolve servicemap' 'resolve servicemap'

# --- 2. systemd ---------------------------------------------------------------
INIT=systemd
seed() { :; }
scene "systemd enables a stopped unit"  'enable_service bluetooth'  'enable bluetooth'
scene "systemd disables a stopped unit" 'disable_service bluetooth' 'disable bluetooth'

seed() { : >"$ROOT/state/ENABLED"; }
scene "enabled but not running is still enabled" \
    'enable_service bluetooth' 'enable bluetooth'
scene "an enabled unit is disabled" 'disable_service bluetooth' 'disable bluetooth'

seed() { : >"$ROOT/state/ENABLED"; : >"$ROOT/state/ACTIVE"; }
scene "enabled + running is skipped"   'enable_service bluetooth' 'enable bluetooth'

# --- 3. openrc, sysvinit, and an init nobody knows ----------------------------
INIT=openrc
seed() { :; }
scene "openrc adds then starts"  'enable_service cups'  'enable cups'
scene "openrc stops then dels"   'disable_service cups' 'disable cups'

INIT=sysvinit
scene "sysvinit enables then starts" 'enable_service cups'  'enable cups'
scene "sysvinit stops then disables" 'disable_service cups' 'disable cups'

INIT=upstart
scene "an unknown init warns on enable"  'enable_service cups'  'enable cups'
scene "an unknown init warns on disable" 'disable_service cups' 'disable cups'

# --- 4. runit -----------------------------------------------------------------
# The link is made only when the package actually shipped /etc/sv/<name>.
INIT=runit
seed() { mkdir -p "$ROOT/sv/bluetoothd"; }
scene "runit links a shipped service" 'enable_service bluetooth' 'enable bluetooth'

seed() { :; }
scene "runit warns when nothing ships one" 'enable_service bluetooth' 'enable bluetooth'

seed() {
    mkdir -p "$ROOT/sv/bluetoothd"
    ln -s "$ROOT/sv/bluetoothd" "$ROOT/service/bluetoothd"
}
scene "an existing link is left alone"  'enable_service bluetooth' 'enable bluetooth'
scene "disable removes the link"        'disable_service bluetooth' 'disable bluetooth'

seed() { :; }
scene "disabling an unlinked service does nothing" \
    'disable_service bluetooth' 'disable bluetooth'

finish
