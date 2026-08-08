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
trap 'rm -rf "$TMP"' EXIT
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

fake_chafa no
if _chafa_ok; then fail "absent chafa reported as ok"; else ok "absent chafa is not ok"; fi
assert_eq "" "$(_chafa_version)" "absent chafa reports an empty version"

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

# --- modules/yazi.sh wiring: the repair path for an ALREADY-installed old chafa
# The case that started this: pkg_install's presence probe is satisfied by the
# distro's 1.14.5, so without this guard the box stays broken forever.
run_step()     { shift; "$@"; }
install_layer() { :; }
as_user()      { :; }
provide_chafa() { echo "BUILD-CHAFA" >>"$OUT"; }
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd); export OSR_DOTFILES
OSR_HOME="$TMP/home"; export OSR_HOME OSR_USER=tester
OSR_RICE_DIR=''

: >"$OUT"; fake_chafa 1.14.5
. "$OSR_ROOT/modules/yazi.sh" >/dev/null 2>&1
assert_contains "$OUT" "BUILD-CHAFA" "module: repairs a pre-existing chafa 1.14.5 (Debian 13)"

: >"$OUT"; fake_chafa 1.18.2
. "$OSR_ROOT/modules/yazi.sh" >/dev/null 2>&1
refute_contains "$OUT" "BUILD-CHAFA" "module: leaves a new-enough chafa alone (§2)"
assert_contains "$OUT" "PKG yazi chafa" "module: still installs yazi + chafa normally"

finish
