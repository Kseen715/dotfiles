#!/bin/sh
# Proves the Fedora yazi route: dnf.map resolves yazi to source:provide_yazi_bin,
# the builder installs BOTH binaries from the upstream release .zip, and it falls
# back to `cargo install yazi-fm/yazi-cli` when the release binary is unusable
# (unsupported arch, or a failed download/extract). Hermetic (no net, no cargo).
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_PKG=dnf
NO_COLOR=1
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/net.sh"; . "$OSR_LIB/pkg.sh"
. "$OSR_LIB/build.sh"
. "$HERE/../lib.sh"

OUT=$(mktemp)
OSR_HOME=$(mktemp -d); export OSR_HOME OSR_USER=tester
mkdir -p "$OSR_HOME/.cargo/bin"; : >"$OSR_HOME/.cargo/bin/cargo"; chmod +x "$OSR_HOME/.cargo/bin/cargo"

as_user() {
    if [ "$1" = test ]; then shift; test "$@"; return $?; fi
    echo "USER $*" >>"$OUT"
}
github_latest() { echo v26.5.6; }
# Stand in for the real fetch+unzip+install; $ZIP_RC decides pass/fail.
_osr_install_zip_bins() { echo "ZIP $*" >>"$OUT"; return "${ZIP_RC:-0}"; }

# Prefix assignments on a *function* persist in POSIX sh, so set the knobs
# (OSR_ARCH / ZIP_RC) as plain vars per scenario rather than inline.
OSR_CODENAME=''; OSR_VERSION_ID=''; OSR_ARCH=''

# --- map row: Fedora takes the builder, not a native package -----------------
assert_eq "source:provide_yazi_bin" "$(_pkgmap_one yazi)" \
    "dnf.map resolves yazi -> source:provide_yazi_bin"

# --- x86_64: prebuilt release zip, both binaries, no cargo -------------------
OSR_ARCH=x86_64; ZIP_RC=0
provide_yazi_bin >/dev/null 2>&1
assert_contains "$OUT" \
    "ZIP https://github.com/sxyazi/yazi/releases/download/v26.5.6/yazi-x86_64-unknown-linux-gnu.zip yazi ya" \
    "x86_64 installs yazi + ya from the upstream release zip"
refute_contains "$OUT" "cargo install" "cargo not touched when the binary route works"

# --- aarch64: same route, arch-correct asset ---------------------------------
: >"$OUT"
OSR_ARCH=aarch64; ZIP_RC=0
provide_yazi_bin >/dev/null 2>&1
assert_contains "$OUT" "yazi-aarch64-unknown-linux-gnu.zip" "aarch64 picks the aarch64 asset"

# --- binary route fails -> cargo fallback (both crates) ----------------------
: >"$OUT"
OSR_ARCH=x86_64; ZIP_RC=1
provide_yazi_bin >/dev/null 2>&1
assert_contains "$OUT" "cargo install --locked yazi-fm" "failed download falls back to cargo (yazi-fm)"
assert_contains "$OUT" "cargo install --locked yazi-cli" "failed download falls back to cargo (yazi-cli)"

# --- no asset for this arch -> cargo, without attempting a download ----------
: >"$OUT"
OSR_ARCH=riscv64; ZIP_RC=0
provide_yazi_bin >/dev/null 2>&1
refute_contains "$OUT" "^ZIP " "unsupported arch does not attempt a release download"
assert_contains "$OUT" "cargo install --locked yazi-fm" "unsupported arch falls back to cargo"

# --- rerun with the binaries already there -> cargo skips (§2) ---------------
: >"$OUT"
: >"$OSR_HOME/.cargo/bin/yazi"; : >"$OSR_HOME/.cargo/bin/ya"
chmod +x "$OSR_HOME/.cargo/bin/yazi" "$OSR_HOME/.cargo/bin/ya"
CAP=$(provide_yazi_bin 2>&1)
printf '%s\n' "$CAP" | grep -q 'skipping' && ok "cargo fallback all-skips on rerun (§2)" || fail "no skip"
refute_contains "$OUT" "cargo install" "cargo not re-run when the binaries exist"

rm -rf "$OSR_HOME"; rm -f "$OUT"
finish
