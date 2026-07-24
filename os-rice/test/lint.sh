#!/bin/sh
# test/lint.sh — POSIX-sh lint for the harness (§9). Every lib/, module,
# runner and bootstrap file must parse under `dash -n` (busybox ash compat) and,
# when available, pass `shellcheck -s sh`. zsh rc.d layers are checked with
# `zsh -n` when zsh is present.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/.." && pwd)
REPO=$(cd -- "$OSR_ROOT/.." && pwd)

SH_CHECKER=$(command -v dash || command -v sh)
FAILED=0

echo "POSIX sh syntax ($SH_CHECKER -n):"
SH_FILES="$OSR_ROOT/install.sh $OSR_ROOT/osr $OSR_ROOT/bootstrap.sh"
SH_FILES="$SH_FILES $(find "$OSR_ROOT/lib" "$OSR_ROOT/modules" "$OSR_ROOT/test" -name '*.sh' 2>/dev/null)"
for f in $SH_FILES; do
    if "$SH_CHECKER" -n "$f" 2>/dev/null; then
        printf '  ok   %s\n' "${f#"$REPO"/}"
    else
        printf '  FAIL %s\n' "${f#"$REPO"/}" >&2
        "$SH_CHECKER" -n "$f" || true
        FAILED=1
    fi
done

if command -v shellcheck >/dev/null 2>&1; then
    echo "shellcheck -s sh:"
    for f in $SH_FILES; do
        if shellcheck -s sh -e SC1090,SC1091 "$f" >/dev/null 2>&1; then
            printf '  ok   %s\n' "${f#"$REPO"/}"
        else
            printf '  WARN %s (shellcheck findings)\n' "${f#"$REPO"/}"
        fi
    done
else
    echo "shellcheck: not installed — skipping"
fi

# ASCII-only program output (§3): every byte the installer writes to the
# terminal must be 7-bit ASCII so barebone TERM/locales never mangle it into
# mojibake. Comments are exempt (prose may use §/em-dashes), so skip comment
# lines; flag any high byte (0x80-0xFF) on a code line. LC_ALL=C keeps the byte
# class portable across gawk/mawk/busybox awk.
echo "ASCII-only program output (non-comment lines):"
# Scope: the installer program (lib + modules + runners), not the test harness
# (matrix.sh legitimately keeps em-dashes in trailing comments).
ASCII_FILES="$OSR_ROOT/install.sh $OSR_ROOT/osr $OSR_ROOT/bootstrap.sh"
ASCII_FILES="$ASCII_FILES $(find "$OSR_ROOT/lib" "$OSR_ROOT/modules" -name '*.sh' 2>/dev/null)"
_ascii_hits=$(LC_ALL=C awk '
    /^[[:space:]]*#/ { next }
    /[\200-\377]/    { printf "  FAIL %s:%d: %s\n", FILENAME, FNR, $0 }
' $ASCII_FILES 2>/dev/null)
if [ -n "$_ascii_hits" ]; then
    printf '%s\n' "$_ascii_hits" >&2
    FAILED=1
else
    echo "  ok   (no non-ASCII bytes in program output)"
fi

if command -v zsh >/dev/null 2>&1; then
    echo "zsh -n (rc.d + rice themes):"
    for f in $(find "$REPO/zsh/rc.d" "$OSR_ROOT/rices" -name '*.zsh' 2>/dev/null); do
        if zsh -n "$f" 2>/dev/null; then
            printf '  ok   %s\n' "${f#"$REPO"/}"
        else
            printf '  FAIL %s\n' "${f#"$REPO"/}" >&2
            FAILED=1
        fi
    done
else
    echo "zsh: not installed — skipping zsh -n"
fi

[ "$FAILED" -eq 0 ] && echo "lint: PASS" || { echo "lint: FAIL" >&2; exit 1; }
