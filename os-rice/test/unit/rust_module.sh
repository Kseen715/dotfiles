#!/bin/sh
# Proves modules/rust.c: installs build tools + rustup as OSR_USER on a fresh
# box, and skips (no network) when cargo is already present (§2 idempotency).
#
# The module is C now, so it runs through the core and the mocks are commands on
# PATH: sudo records what was asked to run as OSR_USER (the "USER ..." lines
# below), dnf records package installs, curl stands in for every fetch.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_PKG=dnf
NO_COLOR=1
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

OUT=$(mktemp)
BIN=$(mktemp -d)
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd); export OSR_DOTFILES

OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip rust_module: %s is not built\n' "$OSR_BIN"
    exit 0
fi

# sudo -u <user> is how the C tier spells as_user, so it is where the "USER"
# lines come from. `test` still runs for real, so the `is cargo there yet`
# probes exercise the real branch; everything else is recorded, not run.
cat >"$BIN/sudo" <<EOF
#!/bin/sh
if [ "\$1" = "-u" ]; then
    shift 2
    if [ "\$1" = test ]; then shift; exec /usr/bin/test "\$@"; fi
    printf 'USER %s\\n' "\$*" >>"$OUT"
    exit 0
fi
exec "\$@"
EOF
printf '#!/bin/sh\ncase "$1" in install) printf "PKG %%s\\n" "$*" >>"%s" ;; esac\nexit 0\n' \
    "$OUT" >"$BIN/dnf"
printf '#!/bin/sh\nexit 1\n' >"$BIN/rpm"
printf '#!/bin/sh\necho RUSTUP-INSTALLER-SCRIPT\nexit 0\n' >"$BIN/curl"
chmod +x "$BIN/sudo" "$BIN/dnf" "$BIN/rpm" "$BIN/curl"
PATH="$BIN:$PATH"; export PATH OSR_ROOT NO_COLOR
run_module() { "$OSR_BIN" module run rust >/dev/null 2>&1 || :; }

# --- scenario 1: fresh box (no cargo) -> rustup gets installed ---------------
: >"$OUT"
OSR_HOME=$(mktemp -d); export OSR_HOME OSR_USER=tester
run_module
assert_contains "$OUT" 'PKG .*curl' "installs build tools via pkg_install"
assert_contains "$OUT" 'USER sh -s -- -y --default-toolchain stable' "rustup installer piped to sh as OSR_USER"
assert_contains "$OUT" 'USER bash' "cargo-binstall release script piped to bash as OSR_USER"
assert_contains "$OUT" "USER $OSR_HOME/.cargo/bin/cargo install --locked cargo-update" "cargo-update installed (no binstall yet -> source)"
assert_contains "$OUT" "USER cp -f $OSR_DOTFILES/cargo/cargo-binstall-shim $OSR_HOME/.local/bin/cargo-binstall-shim" \
    "binstall shim installed for cargo() in 20-aliases.zsh"
assert_contains "$OUT" "USER chmod 0755 $OSR_HOME/.local/bin/cargo-binstall-shim" "shim made executable"
rm -rf "$OSR_HOME"

# --- scenario 2: cargo already present -> skip, no install call --------------
: >"$OUT"
OSR_HOME=$(mktemp -d); export OSR_HOME
mkdir -p "$OSR_HOME/.cargo/bin"; : >"$OSR_HOME/.cargo/bin/cargo"; chmod +x "$OSR_HOME/.cargo/bin/cargo"
CAP=$("$OSR_BIN" module run rust 2>&1 || :)
if printf '%s\n' "$CAP" | grep -q 'skipping'; then
    ok "prints 'skipping' when cargo present"
else
    fail "no skip message"
fi
refute_contains "$OUT" 'USER sh -s' "rustup NOT reinstalled when cargo present (§2)"
rm -rf "$OSR_HOME"

# --- scenario 3: binstall present -> cargo-update rides it, no re-download ---
: >"$OUT"
OSR_HOME=$(mktemp -d); export OSR_HOME
mkdir -p "$OSR_HOME/.cargo/bin"
for _b in cargo cargo-binstall; do : >"$OSR_HOME/.cargo/bin/$_b"; chmod +x "$OSR_HOME/.cargo/bin/$_b"; done
run_module >/dev/null 2>&1
assert_contains "$OUT" "USER $OSR_HOME/.cargo/bin/cargo-binstall --no-confirm cargo-update" "cargo-update installed via binstall"
refute_contains "$OUT" 'USER bash' "binstall NOT reinstalled when present (§2)"
rm -rf "$OSR_HOME"

rm -f "$OUT"; rm -rf "$BIN"
finish
