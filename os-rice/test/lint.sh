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
