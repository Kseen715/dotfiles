#!/bin/sh
# Proves lib/fetch.c fetches what lib/net.sh fetched: the same backend chosen,
# the same command run with the same flags, the same progress lines, and the
# same tag resolved out of a GitHub payload.
#
# Hermetic like test/unit/pkg_c_parity.sh: PATH is reduced to a stub bin/, so
# "does this box have curl" is a property of the scenario. Nothing here talks
# to the network -- every stub answers from a file this test wrote.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip net_c_parity: %s is not built\n' "$OSR_BIN"
    exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
BIN="$TMP/bin"; mkdir -p "$BIN"
LOG="$TMP/log"; export LOG

for _t in sh env cat cut grep sed awk tr head tail printf id mktemp rm cp mkdir \
          sort od wc dirname basename sleep kill stat test true false; do
    _p=$(command -v "$_t" 2>/dev/null) || :
    case "$_p" in
        /*) ln -sf "$_p" "$BIN/$_t" ;;
        *)  for _d in /usr/bin /bin /usr/local/bin; do
                [ -x "$_d/$_t" ] && { ln -sf "$_d/$_t" "$BIN/$_t"; break; }
            done ;;
    esac
done

FACTS="OSR_ROOT=$OSR_ROOT OSR_LIB=$OSR_LIB OSR_DISTRO=ubuntu OSR_ID_LIKE=debian
       OSR_ARCH=x86_64 OSR_USER=tester OSR_HOME=$TMP NO_COLOR=1 TERM=dumb
       OSR_PKG=apt"

# sh_net <script> -- run a snippet with lib/net.sh's functions in scope.
sh_net() {
    # shellcheck disable=SC2086
    env -i PATH="$BIN" LOG="$LOG" $FACTS $EXTRA HOME="$TMP" sh -c '
        . "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/net.sh"
        eval "$1"' _ "$1" 2>&1 || :
}

# c_net <args...> -- the same question, asked of the binary.
c_net() {
    # shellcheck disable=SC2086
    env -i PATH="$BIN" LOG="$LOG" $FACTS $EXTRA HOME="$TMP" \
        "$OSR_BIN" net "$@" 2>&1 || :
}

EXTRA="COLUMNS=80"

# --- 1. which downloader ------------------------------------------------------
# The order is curl, wget, then a busybox that actually carries the applet.
cat >"$BIN/curl" <<'EOF'
#!/bin/sh
printf 'curl %s\n' "$*" >>"$LOG"
EOF
chmod +x "$BIN/curl"
cp "$BIN/curl" "$BIN/wget"; sed -i 's/curl %s/wget %s/' "$BIN/wget"
assert_eq "curl" "$(sh_net 'osr_downloader')" "sh picks curl when it is there"
assert_eq "curl" "$(c_net backend)" "C picks curl too"

rm -f "$BIN/curl"
assert_eq "wget" "$(sh_net 'osr_downloader')" "sh falls back to wget"
assert_eq "wget" "$(c_net backend)" "C falls back to wget"

rm -f "$BIN/wget"
cat >"$BIN/busybox" <<'EOF'
#!/bin/sh
printf 'busybox %s\n' "$*" >>"$LOG"
[ "$1" = wget ] || exit 1
exit 0
EOF
chmod +x "$BIN/busybox"
assert_eq "busybox-wget" "$(sh_net 'osr_downloader')" "sh falls back to busybox wget"
assert_eq "busybox-wget" "$(c_net backend)" "C falls back to busybox wget"

# A busybox WITHOUT the applet is not a downloader.
cat >"$BIN/busybox" <<'EOF'
#!/bin/sh
exit 1
EOF
chmod +x "$BIN/busybox"
assert_eq "" "$(sh_net 'osr_downloader')" "sh: a busybox without wget is no downloader"
assert_eq "" "$(c_net backend)" "C: same"
rm -f "$BIN/busybox"

# --- 2. the fetch command itself ---------------------------------------------
# curl back, and now every call is logged: the flags ARE the port.
cat >"$BIN/curl" <<'EOF'
#!/bin/sh
printf 'curl %s\n' "$*" >>"$LOG"
for a in "$@"; do case "$a" in -o) _next=dest ;; *) [ "${_next:-}" = dest ] && { printf 'PAYLOAD\n' >"$a"; _next=; } ;; esac; done
case "$*" in *-fsSL\ http*) case "$*" in *-o*) : ;; *) printf 'PAYLOAD\n' ;; esac ;; esac
EOF
chmod +x "$BIN/curl"

: >"$LOG"; sh_net 'osr_fetch_stdout https://example.invalid/x >/dev/null'; cp "$LOG" "$TMP/sh.log"
: >"$LOG"; c_net fetch https://example.invalid/x >/dev/null; cp "$LOG" "$TMP/c.log"
assert_eq "$(cat "$TMP/sh.log")" "$(cat "$TMP/c.log")" "fetch: same curl invocation"

# A small download takes the silent path: one curl -o, no progress lines.
EXTRA="COLUMNS=80 OSR_PROGRESS_MIN_BYTES=1048576"
: >"$LOG"; _sh_out=$(sh_net 'osr_download https://example.invalid/small.tar "$OSR_HOME/small.tar" 1024')
cp "$LOG" "$TMP/sh.log"
: >"$LOG"; _c_out=$(c_net download https://example.invalid/small.tar "$TMP/small.tar" 1024)
cp "$LOG" "$TMP/c.log"
assert_eq "$(cat "$TMP/sh.log")" "$(cat "$TMP/c.log")" "download: same curl -o invocation"
assert_eq "$_sh_out" "$_c_out" "download: a small file prints nothing"

# --- 3. the progress lines ----------------------------------------------------
# The payload is already complete when the first poll fires, so both sides
# print exactly one line -- and that line is compared byte for byte, which is
# what the port has to preserve (the fitted name, then the MiB counts).
EXTRA="COLUMNS=80 OSR_PROGRESS_MIN_BYTES=1024 OSR_DOWNLOAD_POLL=1"
cat >"$BIN/curl" <<'EOF'
#!/bin/sh
printf 'curl %s\n' "$*" >>"$LOG"
_dest=
for a in "$@"; do
    [ "${_next:-}" = dest ] && { _dest=$a; _next=; }
    [ "$a" = "-o" ] && _next=dest
done
[ -n "$_dest" ] && dd if=/dev/zero of="$_dest" bs=1024 count=4096 2>/dev/null
exit 0
EOF
chmod +x "$BIN/curl"
ln -sf "$(command -v dd)" "$BIN/dd" 2>/dev/null || :
_sh_out=$(sh_net 'osr_download https://example.invalid/big-release-archive.tar.gz "$OSR_HOME/big.tar" 4194304')
_c_out=$(c_net download https://example.invalid/big-release-archive.tar.gz "$TMP/big2.tar" 4194304)
assert_eq "$_sh_out" "$_c_out" "download: the progress line is byte-identical"
case "$_c_out" in *"MiB (100%)"*) ok "download: it reports the finished size" ;;
    *) fail "download: no finished size in [$_c_out]" ;; esac

# A long name is tail-cropped with "..." so the numbers stay on the line.
# ui.sh derives OSR_COLS itself (`osr ui vars`), so a narrow terminal is
# spelled the way both tiers actually read one: $COLUMNS.
EXTRA="OSR_PROGRESS_MIN_BYTES=1024 OSR_DOWNLOAD_POLL=1 COLUMNS=40"
_long=https://example.invalid/a-very-long-release-asset-name-that-will-not-fit.tar.gz
_sh_out=$(sh_net "osr_download $_long \"\$OSR_HOME/l1.tar\" 4194304")
_c_out=$(c_net download "$_long" "$TMP/l2.tar" 4194304)
assert_eq "$_sh_out" "$_c_out" "download: the same crop on a narrow terminal"
case "$_c_out" in *...*) ok "download: the name really was cropped" ;;
    *) fail "download: the long name was not cropped [$_c_out]" ;; esac

# --- 4. HEAD-derived answers --------------------------------------------------
EXTRA="COLUMNS=80"
cat >"$BIN/curl" <<'EOF'
#!/bin/sh
printf 'curl %s\n' "$*" >>"$LOG"
case "$*" in
    *-fsSLI*)
        printf 'HTTP/1.1 302 Found\r\nContent-Length: 12\r\nLocation: https://cdn.example.invalid/final/tsetup.7.0.9.tar.xz\r\n\r\n'
        printf 'HTTP/1.1 200 OK\r\nContent-Length: 4194304\r\n\r\n'
        ;;
esac
EOF
chmod +x "$BIN/curl"
assert_eq "$(sh_net '_osr_remote_size https://example.invalid/x')" "$(c_net size https://example.invalid/x)" \
    "size: the LAST Content-Length wins"
assert_eq "$(sh_net 'osr_final_url https://example.invalid/x')" "$(c_net final-url https://example.invalid/x)" \
    "final-url: the LAST Location wins"

# --- 5. github_latest ---------------------------------------------------------
cat >"$BIN/curl" <<'EOF'
#!/bin/sh
printf 'curl %s\n' "$*" >>"$LOG"
case "$*" in
    *releases/latest*) printf '{"url":"x","tag_name": "v2.63.0","name":"2.63.0"}\n' ;;
    *"/tags"*)         printf '[{"name":"v9.9.9-tag-fallback"}]\n' ;;
esac
EOF
chmod +x "$BIN/curl"
assert_eq "$(sh_net 'github_latest cli/cli')" "$(c_net github-latest cli/cli)" \
    "github_latest: the release tag"

# No published release: the tags endpoint answers instead.
cat >"$BIN/curl" <<'EOF'
#!/bin/sh
printf 'curl %s\n' "$*" >>"$LOG"
case "$*" in
    *releases/latest*) printf '{"message":"Not Found"}\n' ;;
    *"/tags"*)         printf '[{"name":"v9.9.9-tag-fallback"}]\n' ;;
esac
EOF
chmod +x "$BIN/curl"
assert_eq "$(sh_net 'github_latest cli/cli')" "$(c_net github-latest cli/cli)" \
    "github_latest: falls back to the first tag"

finish
