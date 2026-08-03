#!/bin/sh
# Proves the pipewire/pulseaudio modules are mirror images (each removes the
# other stack before installing its own), that both no-op off pacman, and that
# pkg_remove filters packages that are not installed (a first run must not be
# fatal). Hermetic: no root, no package manager touched.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB
NO_COLOR=1
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

OUT=$(mktemp)
run_step() { shift; "$@"; }
pkg_install() { echo "INSTALL $*" >>"$OUT"; }
pkg_remove()  { echo "REMOVE $*"  >>"$OUT"; }

# --- pacman: each module removes its rival, then installs itself --------------
OSR_PKG=pacman
. "$OSR_ROOT/modules/pipewire.sh"
assert_contains "$OUT" 'REMOVE .*pulseaudio.*jack2' "pipewire removes PulseAudio/JACK"
assert_contains "$OUT" 'INSTALL .*pipewire-pulse.*wireplumber' "pipewire installs its stack"

: >"$OUT"
. "$OSR_ROOT/modules/pulseaudio.sh"
assert_contains "$OUT" 'REMOVE .*pipewire-pulse.*wireplumber' "pulseaudio removes the PipeWire shims"
assert_contains "$OUT" 'INSTALL .*pulseaudio.*jack2' "pulseaudio installs its stack"
# the core `pipewire` package must NOT be in the removal list (portals need it)
assert_eq 0 "$(grep -c '^REMOVE .*[ ]pipewire$\|^REMOVE pipewire ' "$OUT")" \
    "pipewire core kept (xdg-desktop-portal-wlr depends on it)"

# --- non-pacman: both are no-ops ---------------------------------------------
: >"$OUT"
OSR_PKG=apt
. "$OSR_ROOT/modules/pipewire.sh"
. "$OSR_ROOT/modules/pulseaudio.sh"
assert_eq 0 "$(wc -l <"$OUT")" "both modules no-op off pacman"

# --- pkg_remove drops packages that are not installed ------------------------
unset -f pkg_remove
OSR_PKG=pacman
. "$OSR_LIB/pkg.sh"
_native_installed() { [ "$1" = pulseaudio ]; }        # only one of them is there
as_root() { echo "ROOT $*" >>"$OUT"; }
: >"$OUT"
pkg_remove pulseaudio jack2-dbus >/dev/null
assert_contains "$OUT" 'ROOT pacman -R --noconfirm pulseaudio$' "removes only what is installed"

rm -f "$OUT"
finish
