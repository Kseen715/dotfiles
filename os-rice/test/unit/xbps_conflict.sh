#!/bin/sh
# Proves _xbps_clear_conflicts removes exactly the installed packages that would
# abort an xbps transaction, and nothing else.
#
# The case it exists for: unclutter-xfixes provides the virtual `unclutter>=0`,
# so on a box carrying the original `unclutter` xbps refuses the WHOLE batch and
# the other six packages in the same call never land (see lib/pkg.sh). The
# CONFLICT text below is real xbps output, copied from a Void box.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_PKG=xbps
NO_COLOR=1
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/net.sh"; . "$OSR_LIB/pkg.sh"
. "$HERE/../lib.sh"

OUT=$(mktemp)
DRYRUN=$(mktemp)

# Stub the two leaves that need a real Void box: the dry run's report, and root.
as_root() {
    case "$1" in
        xbps-install) cat "$DRYRUN"; return 0 ;;
        *)            echo "ROOT $*" >>"$OUT"; return 0 ;;
    esac
}
_native_held() { [ "$1" = "pinnedpkg" ]; }

# xbps-uhelper and xbps-query are hyphenated, which POSIX sh will not accept as
# function names (dash rejects the file outright), so they are stubbed the only
# portable way: real executables on a PATH of our own, ahead of anything real.
BIN=$(mktemp -d)
cat >"$BIN/xbps-uhelper" <<'STUB'
#!/bin/sh
# getpkgname <name>-<version>_<revision> -> <name>
printf '%s\n' "$2" | sed 's/-[^-]*_[0-9]*$//'
STUB
# Only `needed` has reverse-dependencies; everything else is free to remove.
cat >"$BIN/xbps-query" <<'STUB'
#!/bin/sh
case "$2" in needed) echo "somepkg-1.0_1" ;; esac
STUB
chmod +x "$BIN/xbps-uhelper" "$BIN/xbps-query"
PATH="$BIN:$PATH"; export PATH

# --- the conflict that motivated this: swap it -------------------------------
cat >"$DRYRUN" <<'EOF'
CONFLICT: unclutter-xfixes-1.6_1 with installed pkg unclutter-8_5 (matched by unclutter>=0)
ERROR: Transaction aborted due to conflicting packages.
EOF
: >"$OUT"
_xbps_clear_conflicts i3 unclutter-xfixes xclip >/dev/null 2>&1
assert_contains "$OUT" 'ROOT xbps-remove -y unclutter' "the installed conflicting package is removed"
refute_contains "$OUT" 'xbps-remove -y unclutter-xfixes' "the package being INSTALLED is never the one removed"

# --- no conflict: touch nothing ----------------------------------------------
cat >"$DRYRUN" <<'EOF'
i3-4.25.1_1 install x86_64 https://repo-default.voidlinux.org/current 1 1
EOF
: >"$OUT"
_xbps_clear_conflicts i3 xclip >/dev/null 2>&1
refute_contains "$OUT" 'xbps-remove' "a clean transaction removes nothing"

# --- two NEW packages conflicting: nothing is installed, so nothing to remove -
cat >"$DRYRUN" <<'EOF'
CONFLICT: foo-1_1 with bar-2_1 in transaction
EOF
: >"$OUT"
_xbps_clear_conflicts foo bar >/dev/null 2>&1
refute_contains "$OUT" 'xbps-remove' "an in-transaction conflict is left for xbps to report"

# --- G2: a held package is a stated user decision ----------------------------
cat >"$DRYRUN" <<'EOF'
CONFLICT: newthing-1_1 with installed pkg pinnedpkg-3_2 (matched by pinnedpkg>=0)
EOF
: >"$OUT"
_xbps_clear_conflicts newthing >/dev/null 2>&1
refute_contains "$OUT" 'xbps-remove' "a held package is never removed (G2)"

# --- something else depends on it: not ours to cascade -----------------------
cat >"$DRYRUN" <<'EOF'
CONFLICT: newthing-1_1 with installed pkg needed-3_2 (matched by needed>=0)
EOF
: >"$OUT"
_xbps_clear_conflicts newthing >/dev/null 2>&1
refute_contains "$OUT" 'xbps-remove' "a package with reverse-dependencies is left alone"

rm -rf "$OUT" "$DRYRUN" "$BIN"
finish
