#!/bin/sh
# Proves the portage (Gentoo) native branch of pkg_install: emerge is invoked
# with --noreplace (idempotent), already-installed atoms are filtered, and the
# `build` logical name resolves through portage.map to gcc. Hermetic: emerge/
# qlist are stubs, no real Gentoo needed.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_PKG=portage
NO_COLOR=1
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/net.sh"; . "$OSR_LIB/pkg.sh"
. "$HERE/../lib.sh"

OUT=$(mktemp)
# git already installed, everything else absent; record the emerge invocation.
_native_installed() { case "$1" in dev-vcs/git|git) return 0 ;; *) return 1 ;; esac }
_native_held()      { return 1 ; }
pkg_refresh()       { echo "REFRESH" >>"$OUT"; }
as_root()           { echo "ROOT $*" >>"$OUT"; }

pkg_install zsh git build fastfetch >/dev/null 2>&1

assert_contains "$OUT" 'ROOT emerge --quiet --noreplace --getbinpkg' "emerge uses --noreplace (idempotent) + binhost"
assert_contains "$OUT" 'sys-devel/gcc' "build resolves to gcc via portage.map"
assert_contains "$OUT" 'zsh' "native atom zsh batched into the emerge call"
assert_contains "$OUT" 'fastfetch' "fastfetch batched (native passthrough on portage)"
refute_contains "$OUT" 'git' "already-installed git filtered out (§2)"

rm -f "$OUT"
finish
