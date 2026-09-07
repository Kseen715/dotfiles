#!/bin/sh
# test/matrix-run.sh -- the in-container half of the idempotency test (DESIGN §9).
#
# Runs INSIDE a distro container, against the checkout mounted read-only at
# /dotfiles with a writable tmpfs over os-rice/build. It installs the rice, then
# installs it again, and scores three things:
#
#   install     the first install succeeds
#   idempotent  the second succeeds, is all-skips, and logs no error
#   path        the layered PATH has no duplicate entry after a double-source
#
#   sh /dotfiles/os-rice/test/matrix-run.sh gruvbox
#
# It lives in a file of its own rather than in a shell variable inside
# test/matrix.sh because it now has two callers: matrix.sh, which drives every
# image locally and prints a table, and the CI matrix, which runs one image per
# job and needs nothing but this script's exit status. One copy of what the test
# IS, two ways to launch it.
set -u

RICE=${1:-gruvbox}
I=/dotfiles/os-rice/install.sh
r1=FAIL; r2=FAIL; rp=FAIL

echo "--- first install ---"
if sh "$I" "$RICE" 2>&1; then r1=OK; fi

echo "--- second install (idempotent?) ---"
if sh "$I" "$RICE" >/tmp/i2 2>&1; then
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

# The marker line is what matrix.sh's table reads; the exit status is what CI
# reads. Both say the same thing about the same run.
echo "OSR_MATRIX r1=$r1 r2=$r2 rp=$rp"
[ "$r1" = OK ] && [ "$r2" = OK ] && [ "$rp" = OK ]
