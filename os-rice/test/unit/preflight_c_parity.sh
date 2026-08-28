#!/bin/sh
# Proves lib/preflight.c answers every require: predicate exactly as
# lib/preflight.sh did -- including the `|` alternation, the two-variable
# arch/release checks, the render-node probe, and what happens on a predicate
# this build has never heard of.
#
# Hermetic: PATH is reduced to a stub bin/ so `cmd:` is a property of the
# scenario, and OSR_DRI points at a directory this test creates.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip preflight_c_parity: %s is not built\n' "$OSR_BIN"
    exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
BIN="$TMP/bin"; mkdir -p "$BIN" "$TMP/dri-empty" "$TMP/dri-gpu"
: >"$TMP/dri-gpu/renderD128"
for _t in sh env cat printf grep sed tr cut head tail id dirname basename \
          mktemp rm mkdir test true false; do
    _p=$(command -v "$_t" 2>/dev/null) || :
    case "$_p" in /*) ln -sf "$_p" "$BIN/$_t" ;; esac
done

# The facts a detected host would have exported. DRI defaults to the empty
# directory so gpu:present is false unless a scenario says otherwise.
FACTS="OSR_ROOT=$OSR_ROOT OSR_LIB=$OSR_LIB NO_COLOR=1 TERM=dumb
       OSR_ARCH=x86_64 OSR_ARCH_DEB=amd64 OSR_INIT=runit OSR_DISTRO=void
       OSR_CODENAME=rolling OSR_VERSION_ID=20240314 OSR_DRI=$TMP/dri-empty"
EXTRA=""

sh_pf() {
    # shellcheck disable=SC2086
    env -i PATH="$BIN" $FACTS $EXTRA HOME="$TMP" sh -c '
        . "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/preflight.sh"
        eval "$1"' _ "$1" 2>&1
    printf 'rc=%s\n' "$?"
}

c_pf() {
    # shellcheck disable=SC2086
    env -i PATH="$BIN" $FACTS $EXTRA HOME="$TMP" "$OSR_BIN" preflight "$@" 2>&1
    printf 'rc=%s\n' "$?"
}

# check <predicate> -- both tiers asked the same yes/no question.
check() {
    assert_eq "$(sh_pf "osr_preflight_check '$1'")" "$(c_pf check "$1")" \
        "check $1${2:+ ($2)}"
}

# --- 1. the single-value predicates -------------------------------------------
check arch:x86_64        "the detected arch"
check arch:amd64         "the Debian spelling of it"
check arch:aarch64       "some other arch"
check init:runit
check init:systemd
check distro:void
check distro:debian
check release:rolling    "the codename"
check release:20240314   "the version id"
check release:bookworm
check cmd:sh             "a command that is on PATH"
check cmd:definitely-not-here

# --- 2. alternation -----------------------------------------------------------
check 'distro:void|debian|ubuntu'   "the host is the first branch"
check 'distro:debian|void'          "the host is the last branch"
check 'distro:debian|ubuntu'        "no branch matches"
check 'arch:aarch64|amd64'          "a branch matches the second variable"
check 'init:systemd|openrc|runit'
check 'distro:|void'                "an empty branch is skipped, not matched"
check 'distro:void|'                "a trailing empty branch"
check 'cmd:definitely-not-here|sh'

# --- 3. the GPU probe ---------------------------------------------------------
check gpu:present         "no render node, no count"
EXTRA="OSR_GPU_COUNT=2"
check gpu:present         "detect.sh already counted one"
EXTRA="OSR_GPU_COUNT=0 OSR_DRI=$TMP/dri-gpu"
check gpu:present         "a render node exists"
EXTRA=""

# --- 4. a predicate this build does not know ----------------------------------
check nonsense:whatever   "warns and passes rather than failing closed"
check bareword            "no colon at all"

# --- 5. osr_preflight itself: the first unmet predicate is fatal ---------------
assert_eq "$(sh_pf "osr_preflight distro:void init:runit")" \
          "$(c_pf distro:void init:runit)" "all predicates met"
assert_eq "$(sh_pf "osr_preflight distro:void distro:debian init:runit")" \
          "$(c_pf distro:void distro:debian init:runit)" \
          "the first unmet one ends the run"
assert_eq "$(sh_pf "osr_preflight '' distro:void")" \
          "$(c_pf '' distro:void)" "an empty predicate is skipped"

finish
