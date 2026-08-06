#!/bin/sh
# Proves the cargo: provider: pkg_install dispatches a cargo:<crate> spec to
# `cargo install --locked` as OSR_USER, skips when the binary already exists
# (§2), and errors cleanly when no toolchain is present. Hermetic (no cargo/net).
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_PKG=apt
NO_COLOR=1
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/net.sh"; . "$OSR_LIB/pkg.sh"
. "$HERE/../lib.sh"

OUT=$(mktemp)
OSR_HOME=$(mktemp -d); export OSR_HOME OSR_USER=tester
mkdir -p "$OSR_HOME/.cargo/bin"; : >"$OSR_HOME/.cargo/bin/cargo"; chmod +x "$OSR_HOME/.cargo/bin/cargo"

# as_user: honor `test -x` against the real fs; record cargo invocations.
as_user() {
    if [ "$1" = test ]; then shift; test "$@"; return $?; fi
    echo "USER $*" >>"$OUT"
}
_pkgmap_one() { case "$1" in serie) echo "cargo:serie" ;; *) echo "$1" ;; esac; }

# --- crate absent -> cargo install --locked ----------------------------------
pkg_install serie >/dev/null 2>&1
assert_contains "$OUT" "USER $OSR_HOME/.cargo/bin/cargo install --locked serie" "serie installed via cargo --locked as OSR_USER"

# --- binstall available -> prebuilt binary instead of a source build ---------
: >"$OUT"; : >"$OSR_HOME/.cargo/bin/cargo-binstall"; chmod +x "$OSR_HOME/.cargo/bin/cargo-binstall"
pkg_install serie >/dev/null 2>&1
assert_contains "$OUT" "USER $OSR_HOME/.cargo/bin/cargo binstall --no-confirm serie" "serie installed via cargo-binstall when available"
refute_contains "$OUT" 'install --locked' "no source build when binstall succeeds"
rm -f "$OSR_HOME/.cargo/bin/cargo-binstall"

# --- crate present -> skip ---------------------------------------------------
: >"$OUT"; : >"$OSR_HOME/.cargo/bin/serie"; chmod +x "$OSR_HOME/.cargo/bin/serie"
CAP=$(pkg_install serie 2>&1)
printf '%s\n' "$CAP" | grep -q 'skipping' && ok "skips when crate binary already present (§2)" || fail "no skip"
refute_contains "$OUT" 'install --locked' "cargo not re-run when present"

# --- no toolchain -> clean error ---------------------------------------------
rm -f "$OSR_HOME/.cargo/bin/cargo" "$OSR_HOME/.cargo/bin/serie"
if ( pkg_install serie ) >/dev/null 2>&1; then fail "should error without cargo"; else ok "errors cleanly when no toolchain"; fi

rm -rf "$OSR_HOME"; rm -f "$OUT"
finish
