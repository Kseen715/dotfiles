#!/bin/sh
# Proves modules/thunderbird.c §5/§6 config ownership and the Debian/Ubuntu
# install route: user.js is dotfiles-owned (and carries the Exchange/EWS prefs),
# userChrome.css is rice-owned, both land in every profile, and on apt the
# package resolves to the Mozilla tarball builder instead of the archive's
# snap-stub/ESR. Hermetic (no net/root; the package layer is stubbed).
#
# The module is C now, so it runs through the core: sudo records the escalated
# commands (the "ROOT ..." lines) instead of an as_root shell function, and the
# `thunderbird` on PATH is what keeps the source: probe from starting a real
# tarball install.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_DOTFILES OSR_PKG=apt
NO_COLOR=1; OSR_USER=$(id -un); export OSR_USER   # as_user becomes a no-op
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/user.sh"
. "$OSR_LIB/theme.sh"; . "$OSR_LIB/config.sh"; . "$OSR_LIB/net.sh"
. "$OSR_LIB/pkg.sh"; . "$OSR_LIB/build.sh"
. "$HERE/../lib.sh"

OUT=$(mktemp)
# Fake snap/dpkg on PATH: a machine that HAS the snap and its transitional deb.
BIN=$(mktemp -d)
cat >"$BIN/snap" <<'EOF'
#!/bin/sh
[ "$1" = list ] && exit 0
exit 0
EOF
cat >"$BIN/dpkg" <<'EOF'
#!/bin/sh
[ "$1" = -s ] && { echo "Package: thunderbird"; echo "Version: 2:1snap1-0ubuntu5"; exit 0; }
exit 0
EOF
# sudo records the escalations; nothing here touches the real system. A
# `thunderbird` on PATH answers the source: probe, so no tarball is fetched, and
# its --version is what the "old ESR" scenario turns on.
cat >"$BIN/sudo" <<EOF
#!/bin/sh
if [ "\$1" = "-u" ]; then shift 2; exec "\$@"; fi
printf 'ROOT %s\\n' "\$*" >>"$OUT"
exit 0
EOF
cat >"$BIN/thunderbird" <<'EOF'
#!/bin/sh
echo "Thunderbird ${MOCK_TB_VER:-140.0}"
EOF
printf '#!/bin/sh\nprintf "PKG %%s\\n" "$*" >>"%s"\nexit 0\n' "$OUT" >"$BIN/apt-get"
chmod +x "$BIN/snap" "$BIN/dpkg" "$BIN/sudo" "$BIN/thunderbird" "$BIN/apt-get"
PATH="$BIN:$PATH"; export PATH

OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip thunderbird_module: %s is not built\n' "$OSR_BIN"
    exit 0
fi
export OSR_ROOT NO_COLOR
ERR=$(mktemp)
# stderr goes to $ERR, which is where the ESR warning is looked for below.
run_module() { "$OSR_BIN" module run thunderbird >/dev/null 2>"$ERR" || :; }

# --- pkgmap: apt never takes the archive package -----------------------------
OSR_ARCH=x86_64; export OSR_ARCH
for _c in bullseye bookworm trixie jammy noble resolute; do
    OSR_CODENAME=$_c; export OSR_CODENAME
    assert_eq "source:provide_thunderbird_tarball" "$(_pkgmap_one thunderbird)" \
        "apt/$_c resolves thunderbird to the Mozilla tarball builder (no snap stub, no old ESR)"
done
# A future apt release that ships a real 140+ deb is not listed, so it keeps the
# archive package (native-first, §1a G6) - the module's version guard is the net.
OSR_CODENAME=forky
assert_eq "thunderbird" "$(_pkgmap_one thunderbird)" "an unlisted apt release falls through to the native package"
unset OSR_CODENAME
for _p in pacman dnf xbps apk; do
    OSR_PKG=$_p
    assert_eq "thunderbird" "$(_pkgmap_one thunderbird)" "$_p keeps the native package (current enough for EWS)"
done

# dnf: only the EOL Fedora branches that froze below 140 carry a row. Fedora has
# no VERSION_CODENAME, so the facet is the version_id.
OSR_PKG=dnf
for _v in 38 39 40; do
    OSR_VERSION_ID=$_v; export OSR_VERSION_ID
    assert_eq "source:provide_thunderbird_tarball" "$(_pkgmap_one thunderbird)" \
        "fedora $_v (frozen below 140) takes the tarball"
done
for _v in 41 43 44; do
    OSR_VERSION_ID=$_v; export OSR_VERSION_ID
    assert_eq "thunderbird" "$(_pkgmap_one thunderbird)" "fedora $_v keeps the native package"
done
unset OSR_VERSION_ID
OSR_PKG=apt
if command -v provide_thunderbird_tarball >/dev/null 2>&1; then
    ok "provide_thunderbird_tarball builder is defined (source: row resolves)"
else
    fail "provide_thunderbird_tarball builder missing"
fi

# --- the profile layer: both files, every profile -----------------------------
OSR_HOME=$(mktemp -d); export OSR_HOME
THEME="$OSR_ROOT/themes/xin"; OSR_THEME=xin; OSR_THEME_DIR="$THEME"
export OSR_THEME OSR_THEME_DIR
# Two profiles, as profiles.ini declares them (Path= is relative to the root).
mkdir -p "$OSR_HOME/.thunderbird/aaa.default" "$OSR_HOME/.thunderbird/bbb.work"
printf '[Profile0]\nPath=aaa.default\n\n[Profile1]\nPath=bbb.work\n' \
    >"$OSR_HOME/.thunderbird/profiles.ini"

run_module

# The package step is the source: row's probe, which the thunderbird on PATH
# satisfies - so what is asserted is that the de-snap ran BEFORE it.
assert_contains "$OUT" 'ROOT snap remove --purge thunderbird' \
    "removes the Thunderbird snap"
# De-snap must happen, and BEFORE the install: the source: probe is
# `command -v thunderbird`, which a snap on PATH would satisfy.
assert_contains "$OUT" 'ROOT env DEBIAN_FRONTEND=noninteractive dpkg --purge --force-all thunderbird' \
    "purges the transitional snap-stub deb"
assert_eq "1" "$(grep -n 'PKG thunderbird\|ROOT snap remove' "$OUT" | head -n1 | grep -c 'ROOT snap remove')" \
    "de-snap runs before pkg_install"
for _p in aaa.default bbb.work; do
    assert_contains "$OSR_HOME/.thunderbird/$_p/user.js" \
        'toolkit.legacyUserProfileCustomizations.stylesheets", true' \
        "$_p: user.js enables userChrome.css"
    assert_contains "$OSR_HOME/.thunderbird/$_p/user.js" 'mail.ews.enabled", true' \
        "$_p: user.js turns on the native Exchange/EWS backend"
    assert_contains "$OSR_HOME/.thunderbird/$_p/chrome/userChrome.css" '\-\-osr-accent' \
        "$_p: the xin theme's chrome colors are installed"
done
# The xin sheet targets the modern (115+) panes, so it must not carry dead
# XUL-tree selectors that silently match nothing.
refute_contains "$THEME/config/thunderbird/userChrome.css" '::-moz-tree' \
    "xin userChrome.css has no dead ::-moz-tree- rules"

rm -rf "$OSR_HOME"

# --- an old ESR (a distro that pins one) is called out, not silently themed ---
: >"$OUT"
OSR_PKG=dnf                                   # no de-snap path on dnf
OSR_HOME=$(mktemp -d); export OSR_HOME
printf '#!/bin/sh\necho "Thunderbird 128.4.0"\n' >"$BIN/thunderbird"; chmod +x "$BIN/thunderbird"
run_module
assert_contains "$ERR" 'older than 140' "an ESR below 140 warns that Exchange/EWS is unavailable"
refute_contains "$OUT" 'ROOT snap remove' "no de-snap outside apt"

printf '#!/bin/sh\necho "Thunderbird 152.0"\n' >"$BIN/thunderbird"
: >"$ERR"
run_module
refute_contains "$ERR" 'older than 140' "a current build warns about nothing"

rm -rf "$OSR_HOME" "$BIN"; rm -f "$OUT" "$ERR"
finish
