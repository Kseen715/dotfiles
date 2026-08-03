#!/bin/sh
# test/run.sh — the fast, no-container test suite: POSIX lint + unit tests.
# Runs anywhere (CI per-commit). The docker idempotency matrix is test/matrix.sh.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)

# Same palette as the installer. The unit tests blank OSR_* on purpose (they
# source ui.sh with NO_COLOR=1 so the code under test prints plain text), so the
# terminal decision is handed to test/lib.sh separately.
. "$HERE/../lib/ui.sh"
[ -n "$OSR_GREEN" ] && OSR_TEST_COLOR=1 || OSR_TEST_COLOR=''
export OSR_TEST_COLOR

RC=0
sh "$HERE/lint.sh" || RC=1

echo
printf '%bUnit tests:%b\n' "$OSR_CYAN" "$OSR_NC"
for t in "$HERE"/unit/*.sh; do
    printf '%b- %s%b\n' "$OSR_DIM" "$(basename "$t")" "$OSR_NC"
    if sh "$t"; then :; else RC=1; fi
done

echo
if [ "$RC" -eq 0 ]; then
    printf '%bALL GREEN%b\n' "$OSR_GREEN" "$OSR_NC"
else
    printf '%bSOME FAILED%b\n' "$OSR_RED" "$OSR_NC"
fi
exit "$RC"
