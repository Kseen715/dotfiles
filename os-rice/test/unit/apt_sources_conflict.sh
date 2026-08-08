#!/bin/sh
# Proves _apt_prune_bootstrap_lists. apt 3.0 (Debian 13+) makes one repo
# described twice with different signed-by values a FATAL parse error, so the
# bootstrap list provide_yandex_browser writes takes the WHOLE source list down
# once the vendor postinst adds its own - every later apt call on the box fails
# with an error about a browser repo. The prune hands the repo over as soon as
# the vendor describes it, and runs before any apt call (pkg_refresh).
# Hermetic: /etc/apt is rebased into a sandbox, nothing escalates.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB
NO_COLOR=1
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
OUT="$TMP/out"; : >"$OUT"
SYS="$TMP/etc/apt"; LISTD="$SYS/sources.list.d"

# lib/pkg.sh addresses /etc/apt by absolute path (§5a); under test it runs from a
# copy rebased into the sandbox, so a wrong mock cannot reach the real one.
PKGLIB="$TMP/pkg.sh"
sed "s#/etc/apt#$SYS#g" "$OSR_LIB/pkg.sh" >"$PKGLIB"
. "$PKGLIB"

as_root() { echo "ROOT $*" >>"$OUT"; _c=$1; shift; case "$_c" in rm) rm "$@" ;; esac; }

OURS="$LISTD/yandex-browser.list"
VENDOR="$LISTD/yandex-browser-stable.list"

# fixture <vendor?> - our bootstrap list, optionally alongside the vendor's
fixture() {
    rm -rf "$SYS"; mkdir -p "$LISTD"; : >"$OUT"
    printf 'deb [arch=amd64 signed-by=%s/keyrings/yandex-browser.asc] https://repo.yandex.ru/yandex-browser/deb stable main\n' \
        "$SYS" >"$OURS"
    # The vendor writes a trailing slash on the URI and its own keyring path -
    # that mismatch is the fatal one.
    [ "${1:-}" = vendor ] && printf 'deb [arch=amd64 signed-by=/usr/share/keyrings/yandex-browser.gpg] https://repo.yandex.ru/yandex-browser/deb/ stable main\n' \
        >"$VENDOR"
    return 0
}

OSR_PKG=apt

# --- the conflict: vendor list present -> ours goes ---------------------------
fixture vendor
_apt_prune_bootstrap_lists
if [ -f "$OURS" ]; then fail "bootstrap list kept - apt would still refuse to read any source"
else ok "vendor list present: our bootstrap list is dropped"; fi
[ -f "$VENDOR" ] && ok "vendor list is left alone (it owns the repo now)" \
    || fail "vendor list removed - the browser would stop getting updates"
assert_contains "$OUT" "ROOT rm -f $OURS" "removal goes through as_root"

# --- no vendor list yet -> ours is the only way to reach the repo ------------
fixture
_apt_prune_bootstrap_lists
[ -f "$OURS" ] && ok "no vendor list: ours is kept (it is the only route to the repo)" \
    || fail "bootstrap list dropped with nothing to replace it"

# --- rerun is a no-op (§2) ---------------------------------------------------
fixture vendor
_apt_prune_bootstrap_lists
: >"$OUT"
_apt_prune_bootstrap_lists
refute_contains "$OUT" "ROOT" "rerun with the list already gone touches nothing"

# --- deb822 .sources vendor file is recognised too ---------------------------
fixture
cat >"$LISTD/yandex-browser.sources" <<'EOF'
Types: deb
URIs: https://repo.yandex.ru/yandex-browser/deb/
Suites: stable
Components: main
Signed-By: /usr/share/keyrings/yandex-browser.gpg
EOF
_apt_prune_bootstrap_lists
if [ -f "$OURS" ]; then fail "deb822 vendor source not recognised - the fatal pair survives"
else ok "deb822 .sources vendor file counts as the vendor describing the repo"; fi

# --- other package managers are untouched ------------------------------------
fixture vendor
OSR_PKG=dnf
_apt_prune_bootstrap_lists
[ -f "$OURS" ] && ok "non-apt box: prune is a no-op" || fail "prune ran off an apt box"
OSR_PKG=apt

# --- wiring: pkg_refresh prunes BEFORE apt-get update ------------------------
# The repair has to land before the first apt call, or the run dies on a source
# list apt cannot parse - which is exactly how this surfaced (a chafa build).
fixture vendor
: >"$OUT"
pkg_refresh
_first=$(head -n 1 "$OUT")
case "$_first" in
    "ROOT rm -f $OURS") ok "pkg_refresh prunes before it calls apt-get update" ;;
    *)                  fail "first apt action was '$_first', not the prune" ;;
esac
assert_contains "$OUT" "apt-get update" "pkg_refresh still refreshes the index"

finish
