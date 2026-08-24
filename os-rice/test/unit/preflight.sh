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

# --- alternation: `a|b|c` holds when ANY branch does -------------------------
# This is what lets rices/i3-rosemary say `require: distro:void|debian|ubuntu`
# in one line. The host above is arch/x86_64/systemd.
OSR_GPU_COUNT=1
if osr_preflight distro:void\|debian\|arch >/dev/null 2>&1; then
    ok "alternation matches on a later branch"
else fail "distro:void|debian|arch should match distro=arch"; fi

if osr_preflight distro:arch\|debian >/dev/null 2>&1; then
    ok "alternation matches on the first branch"
else fail "distro:arch|debian should match distro=arch"; fi

# No branch matches -> still an error, and the message keeps the whole predicate
# so the log says what the rice actually asked for, not one arbitrary branch.
OUT=$( osr_preflight distro:void\|debian\|ubuntu 2>&1 ) && RC=0 || RC=$?
assert_eq 1 "$RC" "alternation with no matching branch exits non-zero"
case "$OUT" in *"rice needs 'distro:void|debian|ubuntu'"*)
                   ok "unmet alternation reports the whole predicate" ;;
               *) fail "unmet alternation message (got: $OUT)" ;; esac

# Alternation is per-tag, not a free-for-all: a branch is checked as
# `<tag>:<branch>`, so an arch alternation cannot be satisfied by a distro name.
OUT=$( osr_preflight arch:aarch64\|arch 2>&1 ) && RC=0 || RC=$?
assert_eq 1 "$RC" "an arch alternation is not satisfied by a distro value"

# A single value still works unchanged (no '|' -> no recursion).
if osr_preflight distro:arch >/dev/null 2>&1; then ok "a plain predicate is unaffected"
else fail "distro:arch should still match"; fi

finish
