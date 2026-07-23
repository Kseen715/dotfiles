#!/bin/sh
# Proves pkg_install expands via pkgmap, groups by method, and dispatches:
# native batched, script/source each own their probe, held/installed skipped.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_PKG=apt
NO_COLOR=1
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/net.sh"; . "$OSR_LIB/pkg.sh"
. "$HERE/../lib.sh"

OUT=$(mktemp)

# Stub effectful leaves and the map so the test is hermetic (no net/root).
_native_installed() { [ "$1" = "curl" ]; }     # curl already present
_native_held()      { [ "$1" = "vim" ]; }       # vim held/pinned
as_root() { echo "NATIVE $*" >>"$OUT"; }
as_user() { echo "USER $*" >>"$OUT"; }
osr_fetch_stdout() { echo "true"; }
build_paru() { echo "BUILD-PARU" >>"$OUT"; }
_pkgmap_one() {
    case "$1" in
        paru)     echo "source:build_paru" ;;
        starship) echo "script:https://example/install.sh --yes" ;;
        build)    echo "build-essential" ;;
        *)        echo "$1" ;;
    esac
}
# starship/paru absent so probes fire; build_paru present.
command() {
    if [ "$1" = "-v" ]; then
        case "$2" in starship|paru) return 1 ;; build_paru) return 0 ;; *) return 0 ;; esac
    fi
}

pkg_install zsh curl vim build starship paru >/dev/null 2>&1

assert_contains "$OUT" 'USER sh -s -- --yes' "starship dispatched via script provider"
assert_contains "$OUT" 'BUILD-PARU' "paru dispatched via source builder"
assert_contains "$OUT" 'NATIVE env DEBIAN_FRONTEND=noninteractive apt-get install -y zsh build-essential' "zsh+build-essential batched into one native call"
refute_contains "$OUT" 'curl' "installed native pkg (curl) not reinstalled"
refute_contains "$OUT" 'vim' "held pkg (vim) skipped (G2)"

rm -f "$OUT"
finish
