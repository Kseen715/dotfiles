#!/bin/sh
# Proves §1a: _pkgmap_one resolves name@facet qualifiers most-specific-first
# (codename > version_id > arch > bare name), the mechanism behind `lsd@jammy`
# (G6) and arch-specific rows (G8). Hermetic: a controlled temp map, no net.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
NO_COLOR=1
TMPLIB=$(mktemp -d); mkdir -p "$TMPLIB/pkgmap"
# Source the libs against the REAL lib/ first: log.sh pulls in ui.sh through
# $OSR_LIB when $OSR_BIN is not already set, so pointing OSR_LIB at the temp
# tree before that made the test pass only when it inherited an OSR_BIN from
# the runner, and die on `sh test/unit/facet_qualifier.sh`. _pkgmap_one reads
# $OSR_LIB/pkgmap/ at CALL time, so the swap below still gives it the temp map.
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_PKG=apt
. "$OSR_ROOT/lib/ui.sh"; . "$OSR_ROOT/lib/log.sh"; . "$OSR_ROOT/lib/pkg.sh"
OSR_LIB="$TMPLIB"; export OSR_LIB
. "$HERE/../lib.sh"

cat >"$TMPLIB/pkgmap/apt.map" <<'EOF'
foo = foo-bare
foo@aarch64 = foo-arm
foo@22.04 = foo-jammy-ver
foo@jammy = source:provide_foo

bar = bar-bare
bar@3 = bar-major
bar@3.21 = bar-minor
bar@3.21.3 = bar-point

baz = baz-bare
baz@3.20 = baz-prefix
baz@<=3.22 = baz-old
baz@<4 = baz-older

qux = qux-bare
qux@>24.04 = qux-newer
qux@>=2 = qux-new
EOF

# codename beats version beats arch beats bare.
OSR_CODENAME=jammy; OSR_VERSION_ID=22.04; OSR_ARCH=aarch64
assert_eq "source:provide_foo" "$(_pkgmap_one foo)" "codename facet wins (most specific)"

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

# --- version facets: dotted prefixes (§1a) -----------------------------------
# A distro that reports a patch level (Alpine's VERSION_ID=3.21.3) would need a
# key per point release without these, so `name@3.21` covers all of 3.21.x and
# `name@3` the whole series - longest prefix first, and always after the exact
# key for the full version.
OSR_CODENAME=""; OSR_ARCH=""
OSR_VERSION_ID=3.21.3
assert_eq "bar-point" "$(_pkgmap_one bar)" "exact version key wins over its prefixes"
OSR_VERSION_ID=3.21.9
assert_eq "bar-minor" "$(_pkgmap_one bar)" "3.21.9 falls to the 3.21 prefix"
OSR_VERSION_ID=3.21
assert_eq "bar-minor" "$(_pkgmap_one bar)" "3.21 itself matches the 3.21 row exactly"
OSR_VERSION_ID=3.9.1
assert_eq "bar-major" "$(_pkgmap_one bar)" "3.9.1 falls past 3.9 to the 3 prefix"
OSR_VERSION_ID=4.1.0
assert_eq "bar-bare" "$(_pkgmap_one bar)" "4.1.0 matches no prefix row -> bare"
OSR_VERSION_ID=3.210
assert_eq "bar-major" "$(_pkgmap_one bar)" "3.210 is not 3.21 (components, not string prefixes)"

# --- version facets: comparisons ---------------------------------------------
# The question a map row usually wants to ask is "is this release old enough to
# need the fallback", which no exact key can express.
OSR_VERSION_ID=3.21.3
assert_eq "baz-old" "$(_pkgmap_one baz)" "3.21.3 takes the first matching range (<=3.22)"
OSR_VERSION_ID=3.22
assert_eq "baz-old" "$(_pkgmap_one baz)" "3.22 satisfies <=3.22 (the boundary is inclusive)"
OSR_VERSION_ID=3.23
assert_eq "baz-older" "$(_pkgmap_one baz)" "3.23 misses <=3.22 and takes <4"
OSR_VERSION_ID=4.0
assert_eq "baz-bare" "$(_pkgmap_one baz)" "4.0 satisfies neither range -> bare"
OSR_VERSION_ID=3.20.5
assert_eq "baz-prefix" "$(_pkgmap_one baz)" "a prefix key outranks a range that also matches"

OSR_VERSION_ID=1.9
assert_eq "qux-bare" "$(_pkgmap_one qux)" "1.9 misses >=2"
OSR_VERSION_ID=2
assert_eq "qux-new" "$(_pkgmap_one qux)" "2 satisfies >=2 (inclusive)"
OSR_VERSION_ID=24.04
assert_eq "qux-new" "$(_pkgmap_one qux)" "24.04 is not > 24.04, so the tighter row is skipped"
OSR_VERSION_ID=24.10
assert_eq "qux-newer" "$(_pkgmap_one qux)" "24.10 > 24.04 (04 is decimal 4, not a string; tighter row first)"

# --- the comparison primitives themselves ------------------------------------
# `_ver_cmp a b && r=0 || r=$?` and not `_ver_cmp a b; r=$?`: this file runs
# under `set -e`, where a bare 1/2 exit status would end the test run.
_cmp() { _ver_cmp "$1" "$2" && printf 0 || printf '%s' "$?"; }
assert_eq "1" "$(_cmp 3.21.3 3.21)" "3.21.3 > 3.21 (missing components are 0)"
assert_eq "2" "$(_cmp 3.21 3.21.3)" "3.21 < 3.21.3 (the same, mirrored)"
assert_eq "0" "$(_cmp 3 3.0.0)"     "3 == 3.0.0"
assert_eq "0" "$(_cmp 24.04 24.4)"  "24.04 == 24.4 (leading zeros are decimal)"
assert_eq "0" "$(_cmp 15-SP5 15)"   "a component keeps its leading digits only (15-SP5)"
_ver_match 3.21.3 "<=3.22" && ok "_ver_match: 3.21.3 <= 3.22" || fail "_ver_match: 3.21.3 <= 3.22"
_ver_match 3.21.3 "3.22"  && fail "_ver_match accepted a non-range expression" \
                          || ok "_ver_match: a plain version is not a range"
_ver_match 3.21.3 "<"     && fail "_ver_match accepted a bare operator" \
                          || ok "_ver_match: an operator with no version is not a range"
assert_eq "3.21 3" "$(_ver_prefixes 3.21.3 | sed 's/[[:space:]]*$//')" \
    "_ver_prefixes: longest first, the version itself excluded"

# --- the C tier resolves all of it identically (§C parity) -------------------
# lib/module.c carries its own copy of this ladder; a divergence there sends the
# compiled path to a different package than the shell path chose.
OSR_BIN_C="$OSR_ROOT/build/osr"
if [ -x "$OSR_BIN_C" ]; then
    for _case in "bar 3.21.3" "bar 3.21.9" "bar 3.9.1" "bar 4.1.0" "bar 3.210" \
                 "baz 3.21.3" "baz 3.23" "baz 4.0" "baz 3.20.5" \
                 "qux 1.9" "qux 24.04" "qux 24.10" "foo 22.04"; do
        _n=${_case% *}; _v=${_case#* }
        OSR_VERSION_ID=$_v
        _sh=$(_pkgmap_one "$_n")
        _c=$(env OSR_LIB="$TMPLIB" OSR_PKG=apt OSR_CODENAME='' OSR_VERSION_ID="$_v" \
                 OSR_ARCH='' "$OSR_BIN_C" module pkgmap "$_n" 2>/dev/null || printf 'MISSING')
        assert_eq "$_sh" "$_c" "C parity: $_n @ $_v"
    done
else
    printf '  SKIP build/osr not built - C parity of the facet ladder unchecked\n'
fi

rm -rf "$TMPLIB"
finish
