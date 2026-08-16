#!/bin/sh
# Proves osr_download's progress readout: the total comes from the caller or,
# failing that, from a Content-Length probe, so any big download gets a meter;
# it prints plain newline-terminated percent lines (what the run_step live window
# can render), crops the filename rather than the numbers on a narrow terminal,
# stays silent under the size threshold, and still returns the fetch's exit
# status. Hermetic: the backend is stubbed, nothing leaves the box.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB
NO_COLOR=1
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/net.sh"
. "$HERE/../lib.sh"

SANDBOX=$(mktemp -d)
OSR_DOWNLOAD_POLL=1; export OSR_DOWNLOAD_POLL   # 3s in production, 1s here
osr_ensure_downloader() { :; }

# --- header parsing (the real _osr_remote_size, a stubbed curl) --------------
# Done first, while the function is still the shipped one: the sections below
# replace it to drive osr_download.
HEADERS=$(printf 'HTTP/1.1 302 Found\r\nLocation: https://cdn.example.invalid/big.tar.gz\r\nContent-Length: 0\r\n\r\nHTTP/1.1 200 OK\r\ncontent-length: 1082236229\r\nContent-Type: application/gzip\r\n')
osr_downloader() { echo curl; }
curl() { printf '%s' "$HEADERS"; }
assert_eq "1082236229" "$(_osr_remote_size https://example.invalid/big.tar.gz)" \
    "the last Content-Length wins (redirect hops report their own) and case does not matter"
HEADERS=$(printf 'HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n')
assert_eq "" "$(_osr_remote_size https://example.invalid/big.tar.gz)" \
    "a chunked response yields no size instead of a bogus one"

# The threshold is what the sections below exercise around, so pin it here
# rather than depending on the shipped 8 MiB.
OSR_PROGRESS_MIN_BYTES=1048576                  # 1 MiB

# A "download" that grows the file in four steps, like a real one does.
TOTAL=4194304                                   # 4 MiB
_osr_download_run() {
    _i=0
    : >"$2"
    while [ "$_i" -lt 4 ]; do
        dd if=/dev/zero bs=1048576 count=1 2>/dev/null >>"$2"
        _i=$((_i + 1))
        sleep 1
    done
    return "${FAKE_RC:-0}"
}

OUT="$SANDBOX/progress"
OSR_COLS=80
osr_download https://example.invalid/big.tar.gz "$SANDBOX/big.tar.gz" "$TOTAL" >"$OUT" 2>&1
assert_contains "$OUT" "MiB / 4 MiB" "the readout names both the current and the total size"
assert_contains "$OUT" "^  big\.tar\.gz [0-9]" "each line is prefixed with the file being fetched"
assert_contains "$OUT" "%)" "the readout carries a percentage"
_lines=$(wc -l <"$OUT" | tr -d ' ')
[ "$_lines" -ge 2 ] && ok "progress arrives as several separate lines ($_lines), not one \\r-redrawn one" \
    || fail "expected repeated progress lines, got $_lines"
refute_contains "$OUT" "$(printf '\r')" "no carriage returns (the live window is a tail, it cannot redraw)"

# --- a narrow terminal crops the NAME, never the numbers ---------------------
NARROW="$SANDBOX/narrow"
OSR_COLS=40
osr_download \
    "https://example.invalid/datagrip-2026.2.3-with-a-very-long-vendor-filename.tar.gz?token=abc" \
    "$SANDBOX/big.tar.gz" "$TOTAL" >"$NARROW" 2>&1
OSR_COLS=80
_widest=$(awk '{ if (length($0) > m) m = length($0) } END { print m + 0 }' "$NARROW")
[ "$_widest" -le 40 ] && ok "every line fits the terminal ($_widest <= 40 columns)" \
    || fail "a progress line overflowed the terminal ($_widest > 40 columns)"
assert_contains "$NARROW" '\.\.\. ' "the over-long name is cropped with a marker"
assert_contains "$NARROW" "MiB / 4 MiB" "the byte counts survive the crop"
refute_contains "$NARROW" "token=abc" "the query string is not part of the displayed name"

# A failing fetch must still fail, progress or not.
FAKE_RC=7
if osr_download https://example.invalid/big.tar.gz "$SANDBOX/big.tar.gz" "$TOTAL" >/dev/null 2>&1; then
    fail "a failing download must not be masked by the progress wrapper"
else
    ok "the fetch's exit status survives the progress wrapper"
fi
FAKE_RC=0

# --- no size from the caller: the HEAD probe supplies one --------------------
# Every builder gets a meter without knowing its upstream's metadata; a server
# that reports nothing (or something small) still downloads silently.
REMOTE_SIZE=$TOTAL
_osr_remote_size() { printf '%s' "$REMOTE_SIZE"; }

PROBED="$SANDBOX/probed"
osr_download https://example.invalid/big.tar.gz "$SANDBOX/big.tar.gz" >"$PROBED" 2>&1
assert_contains "$PROBED" "MiB / 4 MiB" "a sizeless call takes its total from the Content-Length probe"

QUIET="$SANDBOX/quiet"
REMOTE_SIZE=131072                              # 128 KiB, under the threshold
osr_download https://example.invalid/small.tar.gz "$SANDBOX/small.tar.gz" >"$QUIET" 2>&1
assert_eq "0" "$(wc -c <"$QUIET" | tr -d ' ')" "a small download prints nothing"

REMOTE_SIZE=""                                  # chunked, or a host that rejects HEAD
osr_download https://example.invalid/unknown.tar.gz "$SANDBOX/unknown.tar.gz" >"$QUIET" 2>&1
assert_eq "0" "$(wc -c <"$QUIET" | tr -d ' ')" "an unknown size falls back to the silent fetch, not an error"

rm -rf "$SANDBOX"
finish
