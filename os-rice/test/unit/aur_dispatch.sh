#!/bin/sh
# Proves the aur: provider (§4): pkg_install dispatches an AUR row through the
# detected helper as OSR_USER, skips a package already in the pacman DB, and
# batches native pacman rows separately. Hermetic — no network, no root, no real
# makepkg (paru/yay + pacman are stubbed).
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_PKG=pacman
NO_COLOR=1
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/net.sh"; . "$OSR_LIB/pkg.sh"
. "$HERE/../lib.sh"

OUT=$(mktemp)

# Stub the map: two aur rows (one installed, one not) + two native rows.
_pkgmap_one() {
    case "$1" in
        wleave) echo "aur:wleave" ;;
        vscode) echo "aur:visual-studio-code-insiders-bin" ;;
        steam)  echo "aur:steam" ;;                 # already installed below
        *)      echo "$1" ;;
    esac
}
# pacman -Q <pkg>: only `steam` is already in the DB (skip it); everything else
# absent so native rows install and aur rows build.
pacman() {
    if [ "$1" = "-Q" ]; then [ "$2" = "steam" ] && return 0 || return 1; fi
    return 0
}
# paru present so _osr_aur_helper resolves it; provide_paru present (unused here).
command() {
    if [ "$1" = "-v" ]; then
        case "$2" in paru) return 0 ;; yay) return 1 ;; *) return 0 ;; esac
    fi
}
as_root() { echo "NATIVE $*" >>"$OUT"; }
as_user() { echo "USER $*" >>"$OUT"; }

pkg_install zsh wleave steam vscode discord >/dev/null 2>&1

assert_contains "$OUT" 'USER paru -S --needed --noconfirm wleave' "wleave dispatched via AUR helper"
assert_contains "$OUT" 'USER paru -S --needed --noconfirm visual-studio-code-insiders-bin' "aur pkg name (not logical) passed to helper"
refute_contains "$OUT" 'steam' "already-installed AUR pkg (steam) skipped (§2)"
assert_contains "$OUT" 'NATIVE pacman -S --needed --noconfirm zsh discord' "native rows batched separately from aur"

# No-helper path: unset paru/yay -> pkg_install must error (paru listed first).
# `|| true` sits outside the assignment: error()'s exit makes the substitution
# fail, and under set -e a failed assignment would abort the script otherwise.
NOHELPER=$(
    command() { [ "$1" = "-v" ] && return 1; }
    _osr_aur_helper() { echo ""; }
    _via_aur wleave wleave 2>&1
) || true
case "$NOHELPER" in
    *"no AUR helper"*) ok "missing AUR helper errors clearly" ;;
    *)                 fail "missing AUR helper should error (got: $NOHELPER)" ;;
esac

rm -f "$OUT"
finish
