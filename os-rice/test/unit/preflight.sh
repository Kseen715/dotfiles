#!/bin/sh
# Proves osr_preflight (§10 Tier 1): satisfied predicates pass, an unmet one
# errors (exits non-zero) before any mutation, and an unknown predicate is
# ignored with a warning. Hermetic — detection vars are set by hand.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB
NO_COLOR=1
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/preflight.sh"
. "$HERE/../lib.sh"

# Simulated host: Arch x86_64, systemd, one GPU.
OSR_ARCH=x86_64; OSR_ARCH_DEB=amd64; OSR_INIT=systemd; OSR_DISTRO=arch
OSR_CODENAME=""; OSR_VERSION_ID=""; OSR_GPU_COUNT=1
export OSR_ARCH OSR_ARCH_DEB OSR_INIT OSR_DISTRO OSR_CODENAME OSR_VERSION_ID OSR_GPU_COUNT

# Satisfied predicates -> osr_preflight returns 0, prints nothing to stderr.
if osr_preflight arch:x86_64 init:systemd distro:arch gpu:present >/dev/null 2>&1; then
    ok "all satisfied predicates pass"
else
    fail "satisfied predicates should pass"
fi

# arch:aarch64 on an x86_64 host -> must error (non-zero).
OUT=$( osr_preflight arch:aarch64 2>&1 ) && RC=0 || RC=$?
assert_eq 1 "$RC" "unmet arch predicate exits non-zero"
case "$OUT" in *"rice needs 'arch:aarch64'"*) ok "unmet predicate names itself" ;;
               *) fail "unmet predicate message (got: $OUT)" ;; esac

# OSR_ARCH_DEB alias also satisfies arch:.
if osr_preflight arch:amd64 >/dev/null 2>&1; then ok "arch matches via OSR_ARCH_DEB alias"
else fail "arch:amd64 should match amd64 alias"; fi

# Unknown predicate -> warns, does not fail.
if osr_preflight bogus:thing >/dev/null 2>&1; then ok "unknown predicate ignored (non-fatal)"
else fail "unknown predicate should not fail the run"; fi

# gpu:present with no GPU -> error.
OSR_GPU_COUNT=0
OUT=$( OSR_DRI=/nonexistent osr_preflight gpu:present 2>&1 ) && RC=0 || RC=$?
assert_eq 1 "$RC" "gpu:present errors when no GPU detected"

finish
