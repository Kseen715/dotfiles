#!/bin/sh
# Proves §1a: _pkgmap_one resolves name@facet qualifiers most-specific-first
# (codename > version_id > arch > bare name), the mechanism behind `lsd@jammy`
# (G6) and arch-specific rows (G8). Hermetic: a controlled temp map, no net.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
NO_COLOR=1
TMPLIB=$(mktemp -d); mkdir -p "$TMPLIB/pkgmap"
OSR_LIB="$TMPLIB"; export OSR_LIB OSR_PKG=apt
. "$OSR_ROOT/lib/log.sh"; . "$OSR_ROOT/lib/pkg.sh"
. "$HERE/../lib.sh"

cat >"$TMPLIB/pkgmap/apt.map" <<'EOF'
foo = foo-bare
foo@aarch64 = foo-arm
foo@22.04 = foo-jammy-ver
foo@jammy = source:build_foo
EOF

# codename beats version beats arch beats bare.
OSR_CODENAME=jammy; OSR_VERSION_ID=22.04; OSR_ARCH=aarch64
assert_eq "source:build_foo" "$(_pkgmap_one foo)" "codename facet wins (most specific)"

OSR_CODENAME=noble; OSR_VERSION_ID=22.04; OSR_ARCH=aarch64
assert_eq "foo-jammy-ver" "$(_pkgmap_one foo)" "version facet wins when codename absent"

OSR_CODENAME=noble; OSR_VERSION_ID=24.04; OSR_ARCH=aarch64
assert_eq "foo-arm" "$(_pkgmap_one foo)" "arch facet wins when codename+version absent"

OSR_CODENAME=noble; OSR_VERSION_ID=24.04; OSR_ARCH=x86_64
assert_eq "foo-bare" "$(_pkgmap_one foo)" "bare row is the fallback when no facet matches"

# empty facet vars must not synthesize a spurious 'foo@' key.
OSR_CODENAME=""; OSR_VERSION_ID=""; OSR_ARCH=""
assert_eq "foo-bare" "$(_pkgmap_one foo)" "empty facets fall back to bare"

# unlisted name passes through unchanged (zero-effort common case).
assert_eq "zsh" "$(_pkgmap_one zsh)" "unlisted name passes through unchanged"

rm -rf "$TMPLIB"
finish
