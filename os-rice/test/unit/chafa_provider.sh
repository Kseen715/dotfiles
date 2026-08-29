#!/bin/sh
# Proves the chafa >= CHAFA_MIN route. Yazi drives chafa with --probe, which only
# exists in 1.16.0+; an older chafa exits on the unknown option and the preview
# pane goes blank with no error, so this is a VERSION gate, not a presence one:
#   - apt releases below resolute resolve to source:provide_chafa (native-first)
#   - the builder no-ops when the chafa on PATH is already new enough (§2)
#   - it compiles the upstream tarball otherwise (upstream ships no binary)
#   - every package manager can resolve chafa-build-deps, since modules/yazi.sh
#     may call the builder directly on any of them
# Hermetic: chafa is a PATH fake printing a scripted version, no net, no build.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB
NO_COLOR=1
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/net.sh"; . "$OSR_LIB/pkg.sh"
. "$OSR_LIB/build.sh"
. "$HERE/../lib.sh"

TMP=$(mktemp -d)
# PATH is restored before the cleanup runs: the absent-chafa probes below shrink
# it to the fake-bin dir, and a crash in that window would otherwise leave the
# trap with no `rm` on PATH and the temp dir behind.
ORIG_PATH="$PATH"
trap 'PATH="$ORIG_PATH"; rm -rf "$TMP"' EXIT
OUT="$TMP/out"; : >"$OUT"
BIN="$TMP/bin"; mkdir -p "$BIN"; PATH="$BIN:$PATH"; export PATH

# chafa on PATH, printing whatever version the scenario asks for. `no` removes it.
fake_chafa() {
    if [ "$1" = no ]; then rm -f "$BIN/chafa"; return 0; fi
    printf '#!/bin/sh\nprintf "Chafa version %s\\n"\n' "$1" >"$BIN/chafa"
    chmod +x "$BIN/chafa"
}

# Nothing may build, download or escalate.
pkg_install()      { echo "PKG $*" >>"$OUT"; }
github_latest()    { echo 1.18.2; }
osr_download()     { echo "DL $1" >>"$OUT"; return "${DL_RC:-1}"; }   # 1 -> stop before ./configure
as_root()          { echo "ROOT $*" >>"$OUT"; }
error()            { echo "ERROR $*" >>"$OUT"; return 1; }            # non-fatal here
OSR_CODENAME=''; OSR_VERSION_ID=''; OSR_ARCH=x86_64

# --- version gate ------------------------------------------------------------
fake_chafa 1.14.5
assert_eq "1.14" "$(_chafa_version)" "reads MAJOR.MINOR out of 'Chafa version 1.14.5'"
if _chafa_ok; then fail "1.14.5 accepted (it has no --probe)"; else ok "1.14.5 rejected (Debian 13 / Ubuntu 24.04)"; fi

fake_chafa 1.8.0
if _chafa_ok; then fail "1.8.0 accepted"; else ok "1.8.0 rejected (Ubuntu 22.04)"; fi

fake_chafa 1.16.0
if _chafa_ok; then ok "1.16.0 accepted (the exact minimum)"; else fail "1.16.0 rejected"; fi

fake_chafa 1.18.2
if _chafa_ok; then ok "1.18.2 accepted (Arch/Void/resolute)"; else fail "1.18.2 rejected"; fi

fake_chafa 2.0.0
if _chafa_ok; then ok "2.0.0 accepted (major bump beats the minor floor)"; else fail "2.0.0 rejected"; fi

# "Absent" has to mean absent on the developer's box too: removing the fake is
# not enough when the host itself ships a chafa in /usr/bin. PATH shrinks to the
# fake dir for these two probes only - _chafa_version returns at `command -v`
# with no external tool behind it, and the asserts are shell builtins.
fake_chafa no
_saved_path="$PATH"; PATH="$BIN"
_chafa_ok && _absent_ok=1 || _absent_ok=0
_absent_ver=$(_chafa_version)
PATH="$_saved_path"
[ "$_absent_ok" -eq 0 ] && ok "absent chafa is not ok" || fail "absent chafa reported as ok"
assert_eq "" "$_absent_ver" "absent chafa reports an empty version"

# --- builder: §2 no-op when the chafa present is already good ----------------
: >"$OUT"; fake_chafa 1.18.2
CAP=$(provide_chafa 2>&1)
refute_contains "$OUT" "DL " "new-enough chafa: nothing downloaded"
refute_contains "$OUT" "PKG " "new-enough chafa: no build deps installed"
printf '%s\n' "$CAP" | grep -q 'skipping' && ok "new-enough chafa: says it is skipping" \
    || fail "new-enough chafa: no skip message"

# --- builder: too old -> deps + upstream tarball -----------------------------
: >"$OUT"; fake_chafa 1.14.5
provide_chafa >/dev/null 2>&1 || true
assert_contains "$OUT" "PKG build chafa-build-deps tar xz" "too-old chafa: installs the build deps"
assert_contains "$OUT" \
    "DL https://github.com/hpjansson/chafa/releases/download/1.18.2/chafa-1.18.2.tar.xz" \
    "too-old chafa: fetches the upstream source tarball (no prebuilt binary exists)"

# --- map rows: every apt release below resolute takes the builder ------------
OSR_PKG=apt
for _c in bullseye bookworm trixie jammy noble; do
    OSR_CODENAME=$_c
    assert_eq "source:provide_chafa" "$(_pkgmap_one chafa)" "apt/$_c -> source:provide_chafa"
done
OSR_CODENAME=resolute
assert_eq "chafa" "$(_pkgmap_one chafa)" "apt/resolute stays native (1.18.1 is new enough)"

# --- build deps resolve on EVERY manager (the builder is callable anywhere) --
OSR_CODENAME=''
for _pm in apt dnf apk xbps pacman portage; do
    OSR_PKG=$_pm
    _dep=$(_pkgmap_one chafa-build-deps)
    if [ "$_dep" = "chafa-build-deps" ]; then
        fail "$_pm has no chafa-build-deps row (pkg_install would install nothing)"
    else
        case "$_dep" in
            *glib*) ok "$_pm resolves chafa-build-deps (glib present: chafa's one mandatory dep)" ;;
            *)      fail "$_pm chafa-build-deps lacks glib: $_dep" ;;
        esac
    fi
done

# chafa keeps passing through as a bare name where the archive is current.
for _pm in pacman xbps; do
    OSR_PKG=$_pm
    assert_eq "chafa" "$(_pkgmap_one chafa)" "$_pm keeps chafa native (rolling, 1.18.x)"
done

# --- modules/yazi.c wiring: the repair path for an ALREADY-installed old chafa
# The case that started this: pkg_install's presence probe is satisfied by the
# distro's 1.14.5, so without this guard the box stays broken forever.
#
# The module is C, so it runs through the core in its own stub bin/ and what is
# observed is the STEP it announces - reaching `Building chafa >= ...` is the
# decision under test; whether the build then succeeds is provide_chafa's own
# business and is asserted above.
OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip chafa_provider module wiring: %s is not built\n' "$OSR_BIN"
    finish
fi
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd); export OSR_DOTFILES
OSR_HOME="$TMP/home"; export OSR_HOME
MBIN="$TMP/mbin"; mkdir -p "$MBIN"
for _t in sh env cat grep sed printf id rm mkdir mktemp test true false tee \
          cp chmod cut tr head sort wc dirname basename; do
    _p=$(command -v "$_t" 2>/dev/null) || :
    case "$_p" in /*) ln -sf "$_p" "$MBIN/$_t" ;; esac
done
printf '#!/bin/sh\n[ "$1" = "-u" ] && shift 2\nexec "$@"\n' >"$MBIN/sudo"
printf '#!/bin/sh\n[ "$1" = "-Q" ] && exit 1\nexit 0\n' >"$MBIN/pacman"
# yazi itself is a source: row everywhere (any.map), so one on PATH keeps this
# test to the chafa question.
printf '#!/bin/sh\nexit 0\n' >"$MBIN/yazi"
chmod +x "$MBIN/sudo" "$MBIN/pacman" "$MBIN/yazi"

# run_yazi <chafa-version> — the module with that chafa on PATH.
run_yazi() {
    printf '#!/bin/sh\nprintf "Chafa version %s\\n"\n' "$1" >"$MBIN/chafa"
    chmod +x "$MBIN/chafa"
    env -i PATH="$MBIN" OSR_ROOT="$OSR_ROOT" OSR_LIB="$OSR_LIB" \
        OSR_DOTFILES="$OSR_DOTFILES" OSR_PKG=pacman OSR_ARCH=x86_64 \
        OSR_DISTRO=arch OSR_INIT=systemd OSR_USER=tester \
        OSR_HOME="$OSR_HOME" HOME="$OSR_HOME" NO_COLOR=1 TERM=dumb \
        OSR_VERBOSE=1 "$OSR_BIN" module run yazi >"$OUT" 2>&1 || :
}

run_yazi 1.14.5
assert_contains "$OUT" "Building chafa" "module: repairs a pre-existing chafa 1.14.5 (Debian 13)"

run_yazi 1.18.2
refute_contains "$OUT" "Building chafa" "module: leaves a new-enough chafa alone (§2)"
assert_contains "$OUT" "Installing Yazi" "module: still installs yazi + chafa normally"

finish
