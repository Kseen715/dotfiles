#!/bin/sh
# Proves modules/mirrors.sh (the POSIX port of the legacy standalone
# setup-mirrors.sh): it ranks from the pristine backup, is a stamped no-op on
# rerun, is never fatal when rankmirrors fails, repairs a missing mirrorlist, and
# no-ops on package managers with no in-tree ranker. Hermetic: the module's /etc
# paths are rebased into a sandbox, rankmirrors is a PATH fake, no network.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB
NO_COLOR=1
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
OUT="$TMP/out"
SYSROOT="$TMP/sys"
BIN="$TMP/bin"; mkdir -p "$BIN"; PATH="$BIN:$PATH"; export PATH
OSR_MIRRORS_N=2; export OSR_MIRRORS_N

# The module writes system config by absolute path (§5a). Under test it runs from
# a copy whose /etc prefixes point into the sandbox, so nothing can reach the
# real /etc/pacman.d even if a mock is wrong.
MOD="$TMP/mirrors.sh"
sed "s#/etc/pacman.d#$SYSROOT/etc/pacman.d#g; s#/etc/dnf#$SYSROOT/etc/dnf#g" \
    "$OSR_ROOT/modules/mirrors.sh" >"$MOD"

run_step()    { shift; "$@"; }
pkg_install() { echo "PKG $*" >>"$OUT"; }
pkg_refresh() { echo "REFRESH" >>"$OUT"; }
as_root() {
    echo "ROOT $*" >>"$OUT"
    _cmd=$1; shift
    case "$_cmd" in
        mkdir) mkdir -p "$2" ;;
        touch) mkdir -p "$(dirname "$1")"; touch "$1" ;;
        cp)    _dst=$(eval "echo \${$#}"); mkdir -p "$(dirname "$_dst")"; cp -f "$2" "$_dst" ;;
        tee)   if [ "$1" = -a ]; then shift; mkdir -p "$(dirname "$1")"; cat >>"$1"
               else mkdir -p "$(dirname "$1")"; cat >"$1"; fi ;;
    esac
}
# No network: the "repair a missing mirrorlist" path gets a canned Arch list,
# commented out exactly as archlinux.org publishes it.
osr_download() {
    echo "DOWNLOAD $1" >>"$OUT"
    [ -n "${DOWNLOAD_FAILS:-}" ] && return 1
    printf '#Server = https://mirror.example/$repo/os/$arch\n#Server = https://m2.example/$repo/os/$arch\n' >"$2"
}

# rankmirrors fake: the fastest N Server lines of the file it was handed. Fails
# outright when RANK_FAILS is set.
cat >"$BIN/rankmirrors" <<'EOF'
#!/bin/sh
[ -n "$RANK_FAILS" ] && exit 1
echo "# ranked from: $3"
grep '^Server' "$3" | head -n "$2"
EOF
chmod +x "$BIN/rankmirrors"

ML="$SYSROOT/etc/pacman.d/mirrorlist"

# reset <pkg> [--no-mirrorlist] — fresh sandbox for the next source of the module.
reset() {
    rm -rf "$SYSROOT"; : >"$OUT"
    mkdir -p "$SYSROOT/etc/pacman.d" "$SYSROOT/etc/dnf"
    OSR_PKG=$1; export OSR_PKG
    [ "${2:-}" = --no-mirrorlist ] && return 0
    { echo '# Arch mirrorlist'
      echo 'Server = https://a.example/$repo/os/$arch'
      echo 'Server = https://b.example/$repo/os/$arch'
      echo 'Server = https://c.example/$repo/os/$arch'
    } >"$ML"
}

# go — source the module in a subshell; a fatal error() surfaces as EXIT= in $OUT.
go() { : >"$OUT"; ( . "$MOD" ) >>"$OUT" 2>&1 || echo "EXIT=$?" >>"$OUT"; }

# --- pacman: rank, back up once, refresh -------------------------------------
reset pacman
go

assert_contains "$OUT" 'PKG pacman-contrib' "installs the ranker (pacman-contrib)"
[ -f "$ML.backup" ] && ok "the pristine mirrorlist is backed up" \
                    || fail "the pristine mirrorlist is backed up"
assert_eq "3" "$(grep -c '^Server' "$ML.backup")" \
    "the backup keeps every mirror, not just the ranked ones"
assert_eq "2" "$(grep -c '^Server' "$ML")" "the live list is trimmed to OSR_MIRRORS_N"
assert_contains "$ML" 'ranked from: .*mirrorlist.backup' \
    "ranking reads the pristine backup, not the already-trimmed live list"
assert_contains "$OUT" 'REFRESH' "the package index is refreshed after the swap"
[ -f "$SYSROOT/etc/pacman.d/.osr-mirrors-ranked" ] \
    && ok "a stamp records that ranking happened" \
    || fail "a stamp records that ranking happened"

# --- rerun is a stamped no-op (§2) -------------------------------------------
go
assert_contains "$OUT" 'already ranked' "a second run skips the multi-minute probe"
refute_contains "$OUT" 'PKG pacman-contrib' "and installs nothing again"

: >"$OUT"; ( OSR_MIRRORS_FORCE=1; . "$MOD" ) >>"$OUT" 2>&1
assert_contains "$OUT" 'REFRESH' "OSR_MIRRORS_FORCE=1 re-ranks"

# --- a failing rankmirrors is a warning, never a broken box ------------------
reset pacman
: >"$OUT"; ( RANK_FAILS=1; export RANK_FAILS; . "$MOD" ) >>"$OUT" 2>&1 || echo "EXIT=$?" >>"$OUT"
assert_contains "$OUT" 'keeping the existing mirrorlist' "a failed ranking warns"
refute_contains "$OUT" 'EXIT=' "a failed ranking does not abort the run"
assert_eq "3" "$(grep -c '^Server' "$ML")" "the working mirrorlist is left intact"
[ -f "$SYSROOT/etc/pacman.d/.osr-mirrors-ranked" ] \
    && fail "no stamp is written when ranking failed" \
    || ok "no stamp is written when ranking failed"

# --- a missing mirrorlist is repaired before anything needs pacman -----------
reset pacman --no-mirrorlist
go
assert_contains "$OUT" 'DOWNLOAD https://archlinux.org/mirrorlist/all/' \
    "fetches the full list when none exists"
assert_contains "$ML.backup" '^Server = ' \
    "the fetched list is uncommented (published commented out)"
# The repair must precede the install: pacman cannot fetch pacman-contrib with
# an empty mirrorlist.
_dl=$(grep -n 'DOWNLOAD' "$OUT" | head -n 1 | cut -d: -f1)
_pk=$(grep -n 'PKG pacman-contrib' "$OUT" | head -n 1 | cut -d: -f1)
[ "$_dl" -lt "$_pk" ] && ok "the mirrorlist is repaired before the ranker install" \
                      || fail "the mirrorlist is repaired before the ranker install"

reset pacman --no-mirrorlist
: >"$OUT"; ( DOWNLOAD_FAILS=1; export DOWNLOAD_FAILS; . "$MOD" ) >>"$OUT" 2>&1 || echo "EXIT=$?" >>"$OUT"
assert_contains "$OUT" 'EXIT=' "no mirrorlist and no network is fatal, before any install"
refute_contains "$OUT" 'PKG ' "and nothing was installed on that path"

# --- dnf: flip its own fastest-mirror selector, once -------------------------
reset dnf
printf '[main]\ngpgcheck=1\n' >"$SYSROOT/etc/dnf/dnf.conf"
go
assert_contains "$SYSROOT/etc/dnf/dnf.conf" '^fastestmirror=True$' "dnf fastestmirror enabled"
go
assert_contains "$OUT" 'already configured' "dnf config is not appended twice"

# --- everything else: an honest no-op ----------------------------------------
for p in apt apk xbps portage; do
    reset "$p"
    go
    assert_contains "$OUT" "no in-tree mirror ranker for '$p'" "$p is a clean no-op"
done

finish
