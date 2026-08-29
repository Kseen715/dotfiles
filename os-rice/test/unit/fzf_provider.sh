#!/bin/sh
# Proves the fzf >= FZF_MIN route. The zsh ↑ history picker draws itself with
# --gutter, which only exists in fzf 0.66.0+; an older fzf exits with `unknown
# option: --gutter` and ↑ answers with an error line instead of a window, so this
# is a VERSION gate, not a presence one:
#   - the apt/dnf releases frozen below the floor resolve to source:provide_fzf
#   - the builder no-ops when the fzf on PATH is already new enough (§2)
#   - it downloads the upstream static binary otherwise (no compile anywhere)
#   - modules/zsh.sh repairs a box that already had an old distro fzf
# Hermetic: fzf is a PATH fake printing a scripted version, no net, no install.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB
NO_COLOR=1
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/net.sh"; . "$OSR_LIB/pkg.sh"
. "$OSR_LIB/build.sh"
. "$HERE/../lib.sh"

TMP=$(mktemp -d)
# PATH is restored before the cleanup runs: the absent-fzf probes below shrink it
# to the fake-bin dir, and a crash in that window would otherwise leave the trap
# with no `rm` on PATH and the temp dir behind.
ORIG_PATH="$PATH"
trap 'PATH="$ORIG_PATH"; rm -rf "$TMP"' EXIT
OUT="$TMP/out"; : >"$OUT"
BIN="$TMP/bin"; mkdir -p "$BIN"; PATH="$BIN:$PATH"; export PATH

# fzf on PATH, printing whatever version the scenario asks for. `no` removes it.
fake_fzf() {
    if [ "$1" = no ]; then rm -f "$BIN/fzf"; return 0; fi
    printf '#!/bin/sh\nprintf "%s\\n"\n' "$1" >"$BIN/fzf"
    chmod +x "$BIN/fzf"
}

# Nothing may download, install or escalate.
pkg_install()   { echo "PKG $*" >>"$OUT"; }
github_latest() { echo v0.74.3; }
osr_download()  { echo "DL $1" >>"$OUT"; return "${DL_RC:-1}"; }   # 1 -> stop before install
as_root()       { echo "ROOT $*" >>"$OUT"; }
error()         { echo "ERROR $*" >>"$OUT"; return 1; }            # non-fatal here
OSR_CODENAME=''; OSR_VERSION_ID=''; OSR_ARCH=x86_64

# --- version gate ------------------------------------------------------------
fake_fzf "0.60 (devel)"
assert_eq "0.60" "$(_fzf_version)" "reads MAJOR.MINOR out of '0.60 (devel)'"
if _fzf_ok; then fail "0.60 accepted (it has no --gutter)"; else ok "0.60 rejected (Debian 13 / Ubuntu 25.04)"; fi

fake_fzf "0.29.0 (debian)"
if _fzf_ok; then fail "0.29.0 accepted"; else ok "0.29.0 rejected (Ubuntu 22.04)"; fi

fake_fzf "0.65.2 (f41)"
if _fzf_ok; then fail "0.65.2 accepted (one release under the floor)"; else ok "0.65.2 rejected (Fedora 41)"; fi

fake_fzf "0.66.0 (abcdef12)"
if _fzf_ok; then ok "0.66.0 accepted (the exact minimum)"; else fail "0.66.0 rejected"; fi

fake_fzf "0.74.3 (15f64c49)"
assert_eq "0.74" "$(_fzf_version)" "reads MAJOR.MINOR out of an upstream version line"
if _fzf_ok; then ok "0.74.3 accepted (Arch/Void/Alpine edge)"; else fail "0.74.3 rejected"; fi

fake_fzf "1.0.0 (abcdef12)"
if _fzf_ok; then ok "1.0.0 accepted (major bump beats the minor floor)"; else fail "1.0.0 rejected"; fi

# "Absent" has to mean absent on the developer's box too: removing the fake is
# not enough when the host itself ships an fzf in /usr/bin. PATH shrinks to the
# fake dir for these two probes only - _fzf_version returns at `command -v` with
# no external tool behind it, and the asserts are shell builtins.
fake_fzf no
_saved_path="$PATH"; PATH="$BIN"
_fzf_ok && _absent_ok=1 || _absent_ok=0
_absent_ver=$(_fzf_version)
PATH="$_saved_path"
[ "$_absent_ok" -eq 0 ] && ok "absent fzf is not ok" || fail "absent fzf reported as ok"
assert_eq "" "$_absent_ver" "absent fzf reports an empty version"

# --- builder: §2 no-op when the fzf present is already good ------------------
: >"$OUT"; fake_fzf "0.74.3 (15f64c49)"
CAP=$(provide_fzf 2>&1)
refute_contains "$OUT" "DL " "new-enough fzf: nothing downloaded"
printf '%s\n' "$CAP" | grep -q 'skipping' && ok "new-enough fzf: says it is skipping" \
    || fail "new-enough fzf: no skip message"

# --- builder: too old -> the upstream release binary for this arch -----------
: >"$OUT"; fake_fzf "0.60 (devel)"
provide_fzf >/dev/null 2>&1 || true
assert_contains "$OUT" \
    "DL https://github.com/junegunn/fzf/releases/download/v0.74.3/fzf-0.74.3-linux_amd64.tar.gz" \
    "too-old fzf: fetches the upstream static binary (x86_64 -> Go's amd64)"
refute_contains "$OUT" "PKG " "too-old fzf: installs no build deps (nothing compiles)"

: >"$OUT"; OSR_ARCH=aarch64
provide_fzf >/dev/null 2>&1 || true
assert_contains "$OUT" "fzf-0.74.3-linux_arm64.tar.gz" "aarch64 -> Go's arm64 asset"
OSR_ARCH=x86_64

# --- map rows: the releases frozen below the floor take the builder ----------
OSR_PKG=apt
for _c in bullseye bookworm trixie jammy noble; do
    OSR_CODENAME=$_c
    assert_eq "source:provide_fzf" "$(_pkgmap_one fzf)" "apt/$_c -> source:provide_fzf"
done
OSR_CODENAME=resolute
assert_eq "fzf" "$(_pkgmap_one fzf)" "apt/resolute stays native (0.67.0 is new enough)"
OSR_CODENAME=''

OSR_PKG=dnf
for _v in 39 40 41; do
    OSR_VERSION_ID=$_v
    assert_eq "source:provide_fzf" "$(_pkgmap_one fzf)" "dnf/f$_v -> source:provide_fzf (EOL, frozen under 0.66)"
done
for _v in 42 43 44; do
    OSR_VERSION_ID=$_v
    assert_eq "fzf" "$(_pkgmap_one fzf)" "dnf/f$_v stays native (0.70+ via updates)"
done
OSR_VERSION_ID=''

# apk: one comparison row instead of a key per point release (Alpine's
# VERSION_ID is 3.21.3, not 3.21) - the case the version facets were added for.
OSR_PKG=apk
for _v in 3.20.0 3.21.3 3.22.1; do
    OSR_VERSION_ID=$_v
    assert_eq "source:provide_fzf" "$(_pkgmap_one fzf)" "apk/$_v -> source:provide_fzf (< 3.23, so <= 0.62.0)"
done
for _v in 3.23.0 3.24.1; do
    OSR_VERSION_ID=$_v
    assert_eq "fzf" "$(_pkgmap_one fzf)" "apk/$_v stays native (0.67.0+)"
done
OSR_VERSION_ID=''

# fzf keeps passing through as a bare name where the archive is current.
for _pm in pacman xbps portage; do
    OSR_PKG=$_pm
    assert_eq "fzf" "$(_pkgmap_one fzf)" "$_pm keeps fzf native"
done

# --- modules/zsh.c wiring: the repair path for an ALREADY-installed old fzf ---
# The case that started this: pkg_install's presence probe is satisfied by the
# distro's 0.60, so without this guard the up-arrow picker stays broken forever.
#
# The module is C, so it runs through the core in its own stub bin/ and what is
# observed is the STEP it announces - reaching `Installing fzf >= ...` is the
# decision under test; whether the download then succeeds is provide_fzf's own
# business and is asserted above.
OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip fzf_provider module wiring: %s is not built\n' "$OSR_BIN"
    finish
fi
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd); export OSR_DOTFILES
OSR_HOME="$TMP/home"; export OSR_HOME
MBIN="$TMP/mbin"; mkdir -p "$MBIN"
for _t in sh env cat grep sed printf id rm mkdir mktemp test true false tee \
          cp chmod cut tr head sort wc dirname basename git; do
    _p=$(command -v "$_t" 2>/dev/null) || :
    case "$_p" in /*) ln -sf "$_p" "$MBIN/$_t" ;; esac
done
printf '#!/bin/sh\n[ "$1" = "-u" ] && shift 2\nexec "$@"\n' >"$MBIN/sudo"
printf '#!/bin/sh\n[ "$1" = "-Q" ] && exit 1\nexit 0\n' >"$MBIN/pacman"
chmod +x "$MBIN/sudo" "$MBIN/pacman"

# run_zsh <fzf-version> — the module with that fzf on PATH. The steps after the
# guard (oh-my-zsh, the plugins) need the network and are expected to fail; the
# run is read for the line the guard prints, which comes first.
run_zsh() {
    printf '#!/bin/sh\nprintf "%s\\n"\n' "$1" >"$MBIN/fzf"
    chmod +x "$MBIN/fzf"
    env -i PATH="$MBIN" OSR_ROOT="$OSR_ROOT" OSR_LIB="$OSR_LIB" \
        OSR_DOTFILES="$OSR_DOTFILES" OSR_PKG=pacman OSR_ARCH=x86_64 \
        OSR_DISTRO=arch OSR_INIT=systemd OSR_USER=tester \
        OSR_HOME="$OSR_HOME" HOME="$OSR_HOME" NO_COLOR=1 TERM=dumb \
        OSR_VERBOSE=1 "$OSR_BIN" module run zsh >"$OUT" 2>&1 || :
}

run_zsh "0.60 (devel)"
assert_contains "$OUT" "Installing fzf >=" "module: repairs a pre-existing fzf 0.60 (Debian 13)"
assert_contains "$OUT" "Installing zsh and tools" "module: still installs zsh + tools normally"

run_zsh "0.74.3 (15f64c49)"
refute_contains "$OUT" "Installing fzf >=" "module: leaves a new-enough fzf alone (§2)"

finish
