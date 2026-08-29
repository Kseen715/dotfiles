#!/bin/sh
# Proves modules/mirrors.c: it ranks from the pristine backup, is a stamped
# no-op on rerun, is never fatal when rankmirrors fails, repairs a missing
# mirrorlist, and no-ops on package managers with no in-tree ranker. Hermetic:
# the module's /etc paths are pointed into a sandbox with OSR_PACMAN_DIR /
# OSR_DNF_CONF, rankmirrors is a PATH fake, and there is no network — the
# downloader is absent from $BIN, and the one scenario that needs a successful
# fetch supplies a `curl` that writes a canned list.
#
# The module is C now, so it runs through the core; PATH is reduced to a stub
# bin/ and the stubs log their argv. Its parity with the frozen sh original is
# asked in test/unit/module_c_parity.sh.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip mirrors_module: %s is not built\n' "$OSR_BIN"
    exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
OUT="$TMP/out"
SYSROOT="$TMP/sys"
BIN="$TMP/bin"; mkdir -p "$BIN"
LOG="$TMP/log"; export LOG

for _t in sh env cat grep sed printf id rm mkdir mktemp test true false tee \
          cp touch chmod cut tr head sort wc dirname basename; do
    _p=$(command -v "$_t" 2>/dev/null) || :
    case "$_p" in /*) ln -sf "$_p" "$BIN/$_t" ;; esac
done

cat >"$BIN/sudo" <<'EOF'
#!/bin/sh
printf 'sudo %s\n' "$*" >>"$LOG"
[ "$1" = "-u" ] && shift 2
exec "$@"
EOF
chmod +x "$BIN/sudo"

# The package manager. `-Q` says "not installed" so the ranker install is always
# attempted and visible; everything else just logs.
cat >"$BIN/pacman" <<'EOF'
#!/bin/sh
printf 'pacman %s\n' "$*" >>"$LOG"
[ "$1" = "-Q" ] && exit 1
exit 0
EOF
cat >"$BIN/dnf" <<'EOF'
#!/bin/sh
printf 'dnf %s\n' "$*" >>"$LOG"
exit 0
EOF
chmod +x "$BIN/pacman" "$BIN/dnf"

# rankmirrors fake: the fastest N Server lines of the file it was handed, on
# stdout, as the real one prints them. Fails outright when RANK_FAILS is set.
cat >"$BIN/rankmirrors" <<'EOF'
#!/bin/sh
[ -n "$RANK_FAILS" ] && exit 1
echo "# ranked from: $3"
grep '^Server' "$3" | head -n "$2"
EOF
chmod +x "$BIN/rankmirrors"

# The downloader, only linked in for the repair scenario: a canned Arch list,
# commented out exactly as archlinux.org publishes it.
cat >"$TMP/curl.impl" <<'EOF'
#!/bin/sh
printf 'curl %s\n' "$*" >>"$LOG"
_out=""
while [ $# -gt 0 ]; do
    case "$1" in -o) _out=$2; shift ;; esac
    shift
done
[ -n "$_out" ] || exit 1
printf '#Server = https://mirror.example/$repo/os/$arch\n#Server = https://m2.example/$repo/os/$arch\n' >"$_out"
EOF
chmod +x "$TMP/curl.impl"

PAC="$SYSROOT/etc/pacman.d"
ML="$PAC/mirrorlist"
DNFCONF="$SYSROOT/etc/dnf/dnf.conf"

FACTS="OSR_ROOT=$OSR_ROOT OSR_LIB=$OSR_LIB OSR_INIT=systemd OSR_ARCH=x86_64
       OSR_USER=tester OSR_HOME=$TMP/home NO_COLOR=1 TERM=dumb COLUMNS=80
       OSR_LOG=$TMP/run.log OSR_VERBOSE=1 OSR_MIRRORS_N=2"

# reset <pkg> [--no-mirrorlist] — a fresh sandbox for the next run.
reset() {
    rm -rf "$SYSROOT"; : >"$OUT"; : >"$LOG"
    mkdir -p "$PAC" "$SYSROOT/etc/dnf" "$TMP/home"
    PKG=$1
    [ "${2:-}" = --no-mirrorlist ] && return 0
    { echo '# Arch mirrorlist'
      echo 'Server = https://a.example/$repo/os/$arch'
      echo 'Server = https://b.example/$repo/os/$arch'
      echo 'Server = https://c.example/$repo/os/$arch'
    } >"$ML"
}

# go [extra-env...] — run the module; a fatal osr_die surfaces as EXIT= in $OUT.
go() {
    : >"$OUT"; : >"$LOG"
    # shellcheck disable=SC2086
    env -i PATH="$BIN" LOG="$LOG" RANK_FAILS="${RANK_FAILS:-}" $FACTS \
        OSR_PKG="$PKG" OSR_PACMAN_DIR="$PAC" OSR_DNF_CONF="$DNFCONF" \
        HOME="$TMP/home" ${1:-} \
        "$OSR_BIN" module run mirrors >>"$OUT" 2>&1 || echo "EXIT=$?" >>"$OUT"
}

# --- pacman: rank, back up once, refresh -------------------------------------
reset pacman
go

assert_contains "$LOG" 'pacman -S .*pacman-contrib' "installs the ranker (pacman-contrib)"
[ -f "$ML.backup" ] && ok "the pristine mirrorlist is backed up" \
                    || fail "the pristine mirrorlist is backed up"
assert_eq "3" "$(grep -c '^Server' "$ML.backup")" \
    "the backup keeps every mirror, not just the ranked ones"
assert_eq "2" "$(grep -c '^Server' "$ML")" "the live list is trimmed to OSR_MIRRORS_N"
assert_contains "$ML" 'ranked from: .*mirrorlist.backup' \
    "ranking reads the pristine backup, not the already-trimmed live list"
assert_contains "$LOG" 'pacman -Sy' "the package index is refreshed after the swap"
[ -f "$PAC/.osr-mirrors-ranked" ] \
    && ok "a stamp records that ranking happened" \
    || fail "a stamp records that ranking happened"

# --- rerun is a stamped no-op (§2) -------------------------------------------
go
assert_contains "$OUT" 'already ranked' "a second run skips the multi-minute probe"
refute_contains "$LOG" 'pacman-contrib' "and installs nothing again"

go OSR_MIRRORS_FORCE=1
assert_contains "$LOG" 'pacman -Sy' "OSR_MIRRORS_FORCE=1 re-ranks"

# --- a failing rankmirrors is a warning, never a broken box ------------------
reset pacman
RANK_FAILS=1 go
RANK_FAILS=
assert_contains "$OUT" 'keeping the existing mirrorlist' "a failed ranking warns"
refute_contains "$OUT" 'EXIT=' "a failed ranking does not abort the run"
assert_eq "3" "$(grep -c '^Server' "$ML")" "the working mirrorlist is left intact"
[ -f "$PAC/.osr-mirrors-ranked" ] \
    && fail "no stamp is written when ranking failed" \
    || ok "no stamp is written when ranking failed"

# --- a missing mirrorlist is repaired before anything needs pacman -----------
reset pacman --no-mirrorlist
cp "$TMP/curl.impl" "$BIN/curl"
go
assert_contains "$LOG" 'curl .*https://archlinux.org/mirrorlist/all/' \
    "fetches the full list when none exists"
assert_contains "$ML.backup" '^Server = ' \
    "the fetched list is uncommented (published commented out)"
# The repair must precede the install: pacman cannot fetch pacman-contrib with
# an empty mirrorlist.
_dl=$(grep -n 'archlinux.org/mirrorlist' "$LOG" | head -n 1 | cut -d: -f1)
_pk=$(grep -n 'pacman-contrib' "$LOG" | head -n 1 | cut -d: -f1)
[ "$_dl" -lt "$_pk" ] && ok "the mirrorlist is repaired before the ranker install" \
                      || fail "the mirrorlist is repaired before the ranker install"

# ...and with no downloader at all, that path is fatal before any install.
rm -f "$BIN/curl"
reset pacman --no-mirrorlist
go
assert_contains "$OUT" 'EXIT=' "no mirrorlist and no network is fatal, before any install"
refute_contains "$LOG" 'pacman-contrib' "and nothing was installed on that path"

# --- dnf: flip its own fastest-mirror selector, once -------------------------
reset dnf
printf '[main]\ngpgcheck=1\n' >"$DNFCONF"
go
assert_contains "$DNFCONF" '^fastestmirror=True$' "dnf fastestmirror enabled"
go
assert_contains "$OUT" 'already configured' "dnf config is not appended twice"

# --- everything else: an honest no-op ----------------------------------------
for p in apt apk xbps portage; do
    reset "$p"
    go
    assert_contains "$OUT" "no in-tree mirror ranker for '$p'" "$p is a clean no-op"
done

finish
