#!/bin/sh
# Proves lib/nerdfont.c installs a Nerd Font exactly as lib/fonts.sh did: the
# same fontconfig skip probe, the same URL, the same unzip and fc-cache calls
# in the same order, and the same best-effort warning on every failure -- a
# font is cosmetic, so nothing here may ever be fatal.
#
# Hermetic: PATH is a stub bin/, so "does this box have unzip/fc-list" is a
# property of the scenario, and the download is a stub curl serving a zip this
# test built.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip fonts_c_parity: %s is not built\n' "$OSR_BIN"
    exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
BIN="$TMP/bin"; mkdir -p "$BIN"
REAL_SH=$(command -v sh)

for _t in env cat cut grep sed awk tr head tail printf id mktemp rm cp mv mkdir \
          ls find sort wc dirname basename stat sleep kill test true false; do
    _p=$(command -v "$_t" 2>/dev/null) || :
    case "$_p" in
        /*) ln -sf "$_p" "$BIN/$_t" ;;
        *)  for _d in /usr/bin /bin /usr/local/bin; do
                [ -x "$_d/$_t" ] && { ln -sf "$_d/$_t" "$BIN/$_t"; break; }
            done ;;
    esac
done
ln -sf "$REAL_SH" "$BIN/sh"

# curl: logged, and writes a byte or two where the fetch asked for them. The
# -o form is the only one lib/net.sh's download path uses.
cat >"$BIN/curl" <<'EOF'
#!/bin/sh
printf 'curl %s\n' "$*" >>"$LOG"
[ -f "$DL_FAIL" ] && exit 22
_out=""
while [ $# -gt 0 ]; do
    [ "$1" = "-o" ] && { _out=$2; shift; }
    shift
done
[ -n "$_out" ] && printf 'PK\003\004fake-zip\n' >"$_out"
exit 0
EOF

# unzip / fc-cache: logged; unzip lays down one file so the tree shows it.
cat >"$BIN/unzip" <<'EOF'
#!/bin/sh
printf 'unzip %s\n' "$*" >>"$LOG"
[ -f "$UNZIP_FAIL" ] && exit 9
_d=""
while [ $# -gt 0 ]; do
    [ "$1" = "-d" ] && { _d=$2; shift; }
    shift
done
[ -n "$_d" ] && { mkdir -p "$_d"; printf 'glyphs\n' >"$_d/FontFile.ttf"; }
exit 0
EOF

cat >"$BIN/fc-cache" <<'EOF'
#!/bin/sh
printf 'fc-cache %s\n' "$*" >>"$LOG"
exit 0
EOF

# fc-list: the registered families, from a file the scenario writes.
cat >"$BIN/fc-list" <<'EOF'
#!/bin/sh
printf 'fc-list %s\n' "$*" >>"$LOG"
[ -f "$FC_LIST" ] && cat "$FC_LIST"
exit 0
EOF
chmod +x "$BIN/curl" "$BIN/unzip" "$BIN/fc-cache" "$BIN/fc-list"

FACTS="OSR_ROOT=$OSR_ROOT OSR_LIB=$OSR_LIB OSR_DISTRO=void OSR_ARCH=x86_64
       NO_COLOR=1 TERM=dumb OSR_PKG=xbps OSR_NERD_FONT_VERSION=v3.4.0"
ME=$(id -un)

seed() { :; }

run_side() {
    _root=$1; _tier=$2; _cmd=$3
    rm -rf "$_root"; mkdir -p "$_root/home" "$_root/tmp"
    ROOT=$_root; seed
    : >"$_root/log"
    if [ "$_tier" = sh ]; then
        # shellcheck disable=SC2086
        env -i PATH="$BIN" LOG="$_root/log" $FACTS OSR_USER="$ME" \
            OSR_HOME="$_root/home" HOME="$_root/home" TMPDIR="$_root/tmp" \
            FC_LIST="$_root/fc-list.out" DL_FAIL="$_root/DL_FAIL" \
            UNZIP_FAIL="$_root/UNZIP_FAIL" ROOT="$_root" \
            sh -c '
                . "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/user.sh"
                . "$OSR_LIB/net.sh"; . "$OSR_LIB/fonts.sh"
                eval "$1"' _ "$_cmd" 2>&1 || :
    else
        # shellcheck disable=SC2086
        env -i PATH="$BIN" LOG="$_root/log" $FACTS OSR_USER="$ME" \
            OSR_HOME="$_root/home" HOME="$_root/home" TMPDIR="$_root/tmp" \
            FC_LIST="$_root/fc-list.out" DL_FAIL="$_root/DL_FAIL" \
            UNZIP_FAIL="$_root/UNZIP_FAIL" ROOT="$_root" \
            "$OSR_BIN" fonts $_cmd 2>&1 || :
    fi
}

# dump_tree <root> -- what the sandbox holds afterwards. The scratch zip is
# named after the pid, so its presence matters and its name cannot be
# compared: it is reported as a shape, not a path.
dump_tree() {
    (cd "$1" && find home tmp | sed 's/-[0-9][0-9]*\.zip$/-PID.zip/' | sort)
}

scene() {
    _label=$1
    _sh_out=$(run_side "$TMP/a" sh "$2" | sed "s|$TMP/a|ROOT|g")
    _c_out=$(run_side "$TMP/b" c "$3" | sed "s|$TMP/b|ROOT|g")
    assert_eq "$_sh_out" "$_c_out" "$_label: same output"
    assert_eq "$(sed -e "s|$TMP/a|ROOT|g" -e 's/-[0-9][0-9]*\.zip/-PID.zip/' <"$TMP/a/log")" \
              "$(sed -e "s|$TMP/b|ROOT|g" -e 's/-[0-9][0-9]*\.zip/-PID.zip/' <"$TMP/b/log")" \
              "$_label: same commands"
    assert_eq "$(dump_tree "$TMP/a")" "$(dump_tree "$TMP/b")" "$_label: same tree"
}

# --- 1. the happy path --------------------------------------------------------
seed() { :; }
scene "default family installed" 'osr_install_nerd_font' 'nerd'
scene "a named family installed" 'osr_install_nerd_font FiraCode' 'nerd FiraCode'

# --- 2. already registered ----------------------------------------------------
# The probe is `fc-list | grep -qi "<name>.*Nerd"`: the family and then "Nerd"
# later on the SAME line, both case-insensitive.
seed() { printf '/usr/share/fonts/JetBrainsMonoNerdFont-Regular.ttf: JetBrainsMono Nerd Font\n' >"$ROOT/fc-list.out"; }
scene "an installed family is skipped" 'osr_install_nerd_font' 'nerd'

seed() { printf '/f.ttf: jetbrainsmono nerd font mono\n' >"$ROOT/fc-list.out"; }
scene "the probe ignores case" 'osr_install_nerd_font' 'nerd'

seed() { printf '/f.ttf: JetBrainsMono\n/g.ttf: Nerd Something Else\n' >"$ROOT/fc-list.out"; }
scene "family and Nerd on different lines do not match" 'osr_install_nerd_font' 'nerd'

seed() { printf '/f.ttf: DejaVu Sans\n' >"$ROOT/fc-list.out"; }
scene "an unrelated family is not a match" 'osr_install_nerd_font' 'nerd'

# --- 3. the failure paths, all best-effort -----------------------------------
seed() { : >"$ROOT/DL_FAIL"; }
scene "a failed download only warns" 'osr_install_nerd_font' 'nerd'

seed() { : >"$ROOT/UNZIP_FAIL"; }
scene "a failed unzip only warns" 'osr_install_nerd_font' 'nerd'

# No unzip at all: nothing is downloaded either.
rm -f "$BIN/unzip"
seed() { :; }
scene "no unzip, no install attempt" 'osr_install_nerd_font' 'nerd'
cat >"$BIN/unzip" <<'EOF'
#!/bin/sh
printf 'unzip %s\n' "$*" >>"$LOG"
_d=""
while [ $# -gt 0 ]; do
    [ "$1" = "-d" ] && { _d=$2; shift; }
    shift
done
[ -n "$_d" ] && { mkdir -p "$_d"; printf 'glyphs\n' >"$_d/FontFile.ttf"; }
exit 0
EOF
chmod +x "$BIN/unzip"

# No fontconfig at all: the probe cannot run and the cache is not refreshed.
rm -f "$BIN/fc-list" "$BIN/fc-cache"
scene "no fontconfig, install still lands" 'osr_install_nerd_font' 'nerd'

finish
