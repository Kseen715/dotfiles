#!/bin/sh
# test/matrix.sh — the idempotency test (§9). For each distro image, install the
# rice TWICE in one container and score three checks:
#   install     first install succeeds
#   idempotent  second install succeeds, all-skips, zero errors
#   path        layered PATH has no duplicate entries after a double-source
# Covers multiple package managers + POSIX-sh under ash/dash. A colored result
# matrix is printed at the end.
#
#   sh test/matrix.sh                       default images + gruvbox
#   OSR_TEST_IMAGES="alpine:latest" sh test/matrix.sh nord
#
# Rootless podman matches user-mode; docker is the fallback.
set -u
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
REPO=$(cd -- "$HERE/../.." && pwd)

RICE=${1:-gruvbox}
IMAGES=${OSR_TEST_IMAGES:-"ubuntu:jammy ubuntu:noble ubuntu:resolute debian:stable-slim alpine:latest archlinux:latest fedora:latest ghcr.io/void-linux/void-glibc-full:latest"}
ENGINE=$(command -v podman 2>/dev/null || command -v docker 2>/dev/null || true)
[ -n "$ENGINE" ] || { echo "no container engine (podman/docker) found" >&2; exit 1; }

# --- colors (TTY + NO_COLOR aware) -------------------------------------------
# Store REAL escape bytes (not the literal string "\033[..") so they render
# whether emitted by printf, sed, or any other tool — a literal "\033" only
# works with printf %b and shows as junk everywhere else.
_E=$(printf '\033')                  # real ESC byte (also used for cursor control)
if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
    C_RESET="${_E}[0m"; C_BOLD="${_E}[1m"; C_DIM="${_E}[2m"
    C_GREEN="${_E}[32m"; C_RED="${_E}[31m"; C_CYAN="${_E}[36m"; C_YELLOW="${_E}[33m"
else
    C_RESET=''; C_BOLD=''; C_DIM=''
    C_GREEN=''; C_RED=''; C_CYAN=''; C_YELLOW=''
fi
# p interprets \n etc; the color vars already hold real ESC bytes so they pass
# through %b unchanged.
p() { printf '%b' "$*"; }

# --- in-container test: emit one machine-parseable result line ---------------
IN_CONTAINER='
I=/dotfiles/os-rice/install.sh
r1=FAIL; r2=FAIL; rp=FAIL

echo "--- first install ---"
if sh "$I" '"$RICE"' 2>&1; then r1=OK; fi

echo "--- second install (idempotent?) ---"
if sh "$I" '"$RICE"' >/tmp/i2 2>&1; then
    cat /tmp/i2
    if ! grep -q "\[ERROR\]" /tmp/i2 && grep -q "skipping" /tmp/i2; then r2=OK; fi
else
    cat /tmp/i2
fi

echo "--- PATH duplicate check ---"
mkdir -p "$HOME/.cargo/bin" "$HOME/go/bin"
Z="$HOME/.config/osr/zsh/rc.d/00-env.zsh"
SH_BIN=$(command -v zsh || command -v sh)
if [ -f "$Z" ]; then
    P=$("$SH_BIN" -c ". \"$Z\"; . \"$Z\"; printf %s \"\$PATH\"" 2>/dev/null)
    DUP=$(printf "%s" "$P" | tr ":" "\n" | sort | uniq -d | grep -v "^$" || true)
    if [ -z "$DUP" ]; then rp=OK; else echo "duplicate PATH entries: $DUP"; fi
fi

echo "OSR_MATRIX r1=$r1 r2=$r2 rp=$rp"
'

field() { echo "$1" | grep -o "$2=[A-Z]*" | cut -d= -f2; }

# RESULTS accumulates one "image|install|idempotent|path|overall" line per image.
RESULTS=""
FAILS=0
IMGW=5   # width of the image column, >= len("image")
for img in $IMAGES; do
    [ ${#img} -gt "$IMGW" ] && IMGW=${#img}
done

p "\n  ${C_BOLD}${C_CYAN}os-rice${C_RESET} ${C_DIM}idempotency matrix${C_RESET}   ${C_DIM}rice${C_RESET} ${C_BOLD}${RICE}${C_RESET}   ${C_DIM}engine${C_RESET} ${C_BOLD}$(basename "$ENGINE")${C_RESET}\n"

# _spin <pid> <label> <log> : spinner on \r until pid exits, showing the last
# log line as a dim live-activity suffix so long steps never look frozen (TTY).
# Frames are plain ASCII (|/-\) so they render in any font/terminal.
_sp_frames='|/-\'
_spin() {
    _sp_pid=$1; _sp_label=$2; _sp_log=$3; _sp_n=${#_sp_frames}
    while kill -0 "$_sp_pid" 2>/dev/null; do
        _sp_i=1
        while [ "$_sp_i" -le "$_sp_n" ]; do
            _sp_c=$(printf '%s' "$_sp_frames" | cut -c "$_sp_i")
            # last log line, stripped of CRs and truncated, as live context
            _sp_last=$(tail -n 1 "$_sp_log" 2>/dev/null | tr -d '\r' | cut -c1-48)
            printf '\r%s[K  %s%s%s %s  %s%s%s' \
                "$_E" "$C_CYAN" "$_sp_c" "$C_RESET" "$_sp_label" \
                "$C_DIM" "$_sp_last" "$C_RESET"
            kill -0 "$_sp_pid" 2>/dev/null || break
            sleep 0.1; _sp_i=$((_sp_i + 1))
        done
    done
}

p "\n"
for img in $IMAGES; do
    LOG=$(mktemp)
    # Run quietly, capturing all output; show a spinner on a TTY, a plain line
    # otherwise (CI). Full logs are surfaced only on failure.
    if [ -t 1 ]; then
        "$ENGINE" run --rm -e HOME=/root -v "$REPO":/dotfiles:ro "$img" \
            sh -c "$IN_CONTAINER" >"$LOG" 2>&1 &
        _pid=$!
        _spin "$_pid" "$img" "$LOG"
        wait "$_pid" 2>/dev/null || true
        printf '\r%s[K' "$_E"            # clear the spinner line
    else
        printf '  running %s ...\n' "$img"
        "$ENGINE" run --rm -e HOME=/root -v "$REPO":/dotfiles:ro "$img" \
            sh -c "$IN_CONTAINER" >"$LOG" 2>&1 || true
    fi

    marker=$(grep '^OSR_MATRIX ' "$LOG" 2>/dev/null | tail -1)
    r1=$(field "$marker" r1); r2=$(field "$marker" r2); rp=$(field "$marker" rp)
    [ -n "$r1" ] || r1=FAIL; [ -n "$r2" ] || r2=FAIL; [ -n "$rp" ] || rp=FAIL

    if [ "$r1" = OK ] && [ "$r2" = OK ] && [ "$rp" = OK ]; then
        overall=PASS
        p "  ${C_GREEN}+${C_RESET} ${img}\n"
    else
        overall=FAIL; FAILS=$((FAILS + 1))
        p "  ${C_RED}x${C_RESET} ${img}\n"
        # surface the tail of the captured log so a failure is debuggable
        p "${C_DIM}"; tail -n 20 "$LOG" | sed 's/^/      /'; p "${C_RESET}\n"
    fi
    rm -f "$LOG"
    RESULTS="${RESULTS}${img}|${r1}|${r2}|${rp}|${overall}
"
done

# --- render the result matrix (borderless, modern) ---------------------------
# padr <text> <width> : left-justify plain text to width
padr() { printf '%-*s' "$2" "$1"; }
# centr <text> <width> : center plain text
centr() {
    _t=$1; _w=$2; _l=$(( (_w - ${#_t}) / 2 )); _r=$(( _w - ${#_t} - _l ))
    _i=0; while [ "$_i" -lt "$_l" ]; do printf ' '; _i=$((_i + 1)); done
    printf '%s' "$_t"
    _i=0; while [ "$_i" -lt "$_r" ]; do printf ' '; _i=$((_i + 1)); done
}
# glyph <OK|FAIL> <width> : centered colored check/cross
glyph() {
    if [ "$1" = OK ]; then _g="+"; _c=$C_GREEN; else _g="x"; _c=$C_RED; fi
    _w=$2; _l=$(( (_w - 1) / 2 )); _r=$(( _w - 1 - _l ))
    _i=0; while [ "$_i" -lt "$_l" ]; do printf ' '; _i=$((_i + 1)); done
    p "${_c}${_g}${C_RESET}"
    _i=0; while [ "$_i" -lt "$_r" ]; do printf ' '; _i=$((_i + 1)); done
}
# verdict <PASS|FAIL> <width> : centered colored lowercase word
verdict() {
    if [ "$1" = PASS ]; then _v="pass"; _c=$C_GREEN; else _v="fail"; _c=$C_RED; fi
    _w=$2; _l=$(( (_w - 4) / 2 )); _r=$(( _w - 4 - _l ))
    _i=0; while [ "$_i" -lt "$_l" ]; do printf ' '; _i=$((_i + 1)); done
    p "${_c}${C_BOLD}${_v}${C_RESET}"
    _i=0; while [ "$_i" -lt "$_r" ]; do printf ' '; _i=$((_i + 1)); done
}

GAP="   "                            # column gap — airy, no rules or bars
p "\n"
# dim uppercase header, no separator line
p "  ${C_DIM}"; padr IMAGE "$IMGW"; p "$GAP"; centr INSTALL 7; p "$GAP"
centr IDEMPOTENT 10; p "$GAP"; centr PATH 4; p "$GAP"; centr RESULT 6; p "${C_RESET}\n"

printf '%s\n' "$RESULTS" | while IFS='|' read -r img c1 c2 c3 c4; do
    [ -n "$img" ] || continue
    p "  "; padr "$img" "$IMGW"; p "$GAP"
    glyph "$c1" 7; p "$GAP"; glyph "$c2" 10; p "$GAP"; glyph "$c3" 4; p "$GAP"
    verdict "$c4" 6; p "\n"
done

TOTAL=0; for img in $IMAGES; do TOTAL=$((TOTAL + 1)); done
PASSED=$((TOTAL - FAILS))
p "\n"
if [ "$FAILS" -eq 0 ]; then
    p "  ${C_GREEN}+${C_RESET} ${C_BOLD}${PASSED}/${TOTAL}${C_RESET} passed\n"
else
    p "  ${C_RED}x${C_RESET} ${C_BOLD}${FAILS}${C_RESET} failed ${C_DIM}-${C_RESET} ${PASSED}/${TOTAL} passed\n"
fi
[ "$FAILS" -eq 0 ]
