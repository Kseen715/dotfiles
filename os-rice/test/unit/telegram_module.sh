#!/bin/sh
# Proves the Telegram route: the map row resolves to the vendor tarball on every
# manager, the current version comes out of the download link's REDIRECT (no
# version written down anywhere), and provide_telegram is version-idempotent -
# it skips the download when the stamp matches, replaces the tree when it does
# not, and leaves the tree owned by the riced user so Telegram's own updater
# still works. Hermetic (no net, no root, no /opt: the prefix is a sandbox and
# every download is stubbed).
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_DOTFILES OSR_PKG=apt
NO_COLOR=1; OSR_USER=$(id -un); export OSR_USER   # as_user becomes a no-op
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/user.sh"
. "$OSR_LIB/net.sh"; . "$OSR_LIB/pkg.sh"; . "$OSR_LIB/build.sh"
. "$OSR_LIB/theme.sh"; . "$OSR_LIB/config.sh"
. "$HERE/../lib.sh"

SANDBOX=$(mktemp -d)
OUT="$SANDBOX/calls"; : >"$OUT"
OSR_TELEGRAM_PREFIX="$SANDBOX/opt/telegram-desktop"
OSR_HOME="$SANDBOX/home"; mkdir -p "$OSR_HOME"

run_step() { shift; "$@"; }
pkg_install() { echo "PKG $*" >>"$OUT"; }
as_root() {
    case "$1" in
        ln|chown) echo "ROOT $*" >>"$OUT" ;;
        *) "$@" ;;
    esac
}

# --- the redirect: telegram.org/dl/desktop/linux -> tsetup.<version>.tar.xz ---
TG_VERSION=7.0.9
_osr_head() {
    printf 'HTTP/2 302\nlocation: https://td.telegram.org/tlinux/tsetup.%s.tar.xz\n' "$TG_VERSION"
    printf 'HTTP/2 200\ncontent-length: 77785992\n'
}

for _p in apt dnf pacman apk xbps portage; do
    OSR_PKG=$_p
    assert_eq "source:provide_telegram" "$(_pkgmap_one telegram-desktop)" \
        "$_p resolves telegram-desktop to the vendor tarball builder"
done
OSR_PKG=apt

OSR_ARCH=x86_64
assert_eq "7.0.9 https://td.telegram.org/tlinux/tsetup.7.0.9.tar.xz 77785992" \
    "$(_telegram_latest)" "the version and size come out of the redirect, nothing hard-coded"
OSR_ARCH=aarch64
( _telegram_latest >/dev/null 2>&1 ) && fail "an arch Telegram does not ship should error" \
    || ok "an arch Telegram does not ship a tarball for errors instead of 404ing later"
OSR_ARCH=x86_64

# --- a stubbed "download": produce the layout the vendor ships ---------------
# Telegram/ at the root, holding the app binary and its Updater.
osr_download() {
    echo "DOWNLOAD $1 size=$3" >>"$OUT"
    _stage="$SANDBOX/fake"; rm -rf "$_stage"; mkdir -p "$_stage/Telegram"
    printf '#!/bin/sh\n' >"$_stage/Telegram/Telegram"
    printf '#!/bin/sh\n' >"$_stage/Telegram/Updater"
    chmod +x "$_stage/Telegram/Telegram" "$_stage/Telegram/Updater"
    tar -cJf "$2" -C "$_stage" Telegram
}

# --- first install -----------------------------------------------------------
provide_telegram
assert_contains "$OUT" "DOWNLOAD https://td.telegram.org/tlinux/tsetup.7.0.9.tar.xz size=77785992" \
    "a box with no Telegram downloads the current tarball, size in hand for the progress readout"
assert_eq "7.0.9" "$(cat "$OSR_TELEGRAM_PREFIX/.osr-version")" \
    "the installed version is stamped in the tree (the binary has no -version flag)"
[ -x "$OSR_TELEGRAM_PREFIX/Telegram" ] && ok "the app binary lands at the prefix" \
    || fail "no Telegram binary at the prefix"
assert_contains "$OUT" "ROOT ln -sf $OSR_TELEGRAM_PREFIX/Telegram /usr/local/bin/telegram-desktop" \
    "telegram-desktop goes on PATH via /usr/local/bin"
assert_contains "$OUT" "ROOT chown -R $OSR_USER $OSR_TELEGRAM_PREFIX" \
    "the tree is handed to the riced user so Telegram's own updater can write to it"
assert_eq "" "$(find "$SANDBOX/opt" -maxdepth 1 -name '.telegram-*' 2>/dev/null)" \
    "the staging directory is cleaned up"

# --- rerun on a current box: no download, symlink still repaired -------------
: >"$OUT"
provide_telegram
refute_contains "$OUT" "DOWNLOAD" "a tree already at the current version skips the download"
assert_contains "$OUT" "ROOT ln -sf" "the PATH symlink is rewritten even when nothing was downloaded"

# --- upstream moves ahead: the tree is replaced in place ---------------------
: >"$OUT"
TG_VERSION=7.1.0
provide_telegram
assert_contains "$OUT" "DOWNLOAD https://td.telegram.org/tlinux/tsetup.7.1.0.tar.xz" \
    "a behind tree downloads the new release"
assert_eq "7.1.0" "$(cat "$OSR_TELEGRAM_PREFIX/.osr-version")" \
    "the update replaces the tree at the same prefix"
assert_eq "1" "$(find "$SANDBOX/opt" -maxdepth 1 -type d ! -path "$SANDBOX/opt" | wc -l | tr -d ' ')" \
    "no second tree is left behind next to it"

# --- the theme layer is a palette Telegram can actually parse ----------------
# Telegram's own parser needs `name: value;` on every non-comment line, and a
# value is #rrggbb, #rrggbbaa, or a name defined EARLIER in the file. A rendered
# palette that breaks either rule is rejected as a whole, so check both here.
OSR_THEME=nord; OSR_THEME_DIR="$OSR_ROOT/themes/nord"
PALETTE="$SANDBOX/os-rice.tdesktop-palette"
render_theme_template "$OSR_DOTFILES/telegram/os-rice.tdesktop-palette.tmpl" "$PALETTE"
refute_contains "$PALETTE" '{{' "every placeholder in the palette is filled by a real theme"
BAD=$(sed 's|//.*||' "$PALETTE" | grep -v '^[[:space:]]*$' \
    | grep -vc '^[A-Za-z][A-Za-z0-9]*:[[:space:]]*\(#[0-9a-fA-F]\{6\}\([0-9a-fA-F]\{2\}\)\{0,1\}\|[A-Za-z][A-Za-z0-9]*\);$' || :)
assert_eq "0" "$BAD" "every line is 'name: #rrggbb[aa]|name;' - the shape Telegram parses"
# Forward references: collect the names as they are defined, and flag any value
# that names a constant not yet seen (Telegram resolves strictly top-down).
UNDEF=$(sed 's|//.*||' "$PALETTE" | grep -v '^[[:space:]]*$' | awk -F'[:;]' '
    { name = $1; gsub(/[[:space:]]/, "", name); v = $2; gsub(/[[:space:]]/, "", v)
      if (v !~ /^#/ && !(v in seen)) print name " -> " v
      seen[name] = 1 }')
assert_eq "" "$UNDEF" "no value references a constant defined later in the file"

rm -rf "$SANDBOX"
finish
