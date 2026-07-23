#!/bin/sh
# test/run.sh — the fast, no-container test suite: POSIX lint + unit tests.
# Runs anywhere (CI per-commit). The docker idempotency matrix is test/matrix.sh.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)

RC=0
sh "$HERE/lint.sh" || RC=1

echo
echo "Unit tests:"
for t in "$HERE"/unit/*.sh; do
    echo "- $(basename "$t")"
    if sh "$t"; then :; else RC=1; fi
done

echo
[ "$RC" -eq 0 ] && echo "ALL GREEN" || echo "SOME FAILED"
exit "$RC"
