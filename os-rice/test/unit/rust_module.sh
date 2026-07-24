#!/bin/sh
# Proves modules/rust.sh: installs build tools + rustup as OSR_USER on a fresh
# box, and skips (no network) when cargo is already present (§2 idempotency).
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_PKG=dnf
NO_COLOR=1
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

OUT=$(mktemp)

# Hermetic stubs: run_step just runs the wrapped command (drop the description);
# effectful leaves log their args instead of touching net/root/disk.
run_step() { shift; "$@"; }
pkg_install() { echo "PKG $*" >>"$OUT"; }
osr_fetch_stdout() { echo "RUSTUP-INSTALLER-SCRIPT"; }
check_error() { :; }
as_user() {
    # emulate `as_user test -x <path>` against the real filesystem so the probe
    # branch is exercised; everything else just records the call.
    if [ "$1" = test ]; then shift; test "$@"; return $?; fi
    echo "USER $*" >>"$OUT"
}

# --- scenario 1: fresh box (no cargo) -> rustup gets installed ---------------
: >"$OUT"
OSR_HOME=$(mktemp -d); export OSR_HOME OSR_USER=tester
. "$OSR_ROOT/modules/rust.sh"
assert_contains "$OUT" 'PKG build curl' "installs build tools via pkg_install"
assert_contains "$OUT" 'USER sh -s -- -y --default-toolchain stable' "rustup installer piped to sh as OSR_USER"
rm -rf "$OSR_HOME"

# --- scenario 2: cargo already present -> skip, no install call --------------
: >"$OUT"
OSR_HOME=$(mktemp -d); export OSR_HOME
mkdir -p "$OSR_HOME/.cargo/bin"; : >"$OSR_HOME/.cargo/bin/cargo"; chmod +x "$OSR_HOME/.cargo/bin/cargo"
CAP=$(. "$OSR_ROOT/modules/rust.sh" 2>&1)
if printf '%s\n' "$CAP" | grep -q 'skipping'; then
    ok "prints 'skipping' when cargo present"
else
    fail "no skip message"
fi
refute_contains "$OUT" 'USER sh -s' "rustup NOT reinstalled when cargo present (§2)"
rm -rf "$OSR_HOME"

rm -f "$OUT"
finish
