#!/bin/sh
# Proves install_omz's presence probe is the FILE, not the directory, and that
# the stub case is repaired. ~/.oh-my-zsh exists without an omz in it on any box
# where the distro packages omz system-wide (Armbian: /etc/oh-my-zsh, exported as
# ZSH by its stock ~/.zshrc) - install_zsh_plugin still creates
# custom/plugins/ there, and a directory probe would call that "installed",
# leaving `source $ZSH/oh-my-zsh.sh` in 10-omz.zsh with nothing to read and every
# plugin - highlighting, autosuggestions, autocomplete, and with it the up-arrow
# history widget - silently unloaded.
# Hermetic: git and the omz installer are fakes, no net.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB
NO_COLOR=1
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/net.sh"; . "$OSR_LIB/git.sh"
. "$HERE/../lib.sh"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
OUT="$TMP/out"; : >"$OUT"
OSR_HOME="$TMP/home"; export OSR_HOME
OSR_USER=tester

# as_user runs the real command (the fakes below decide what that means), so the
# file moves are exercised for real; git is a fake that materializes a checkout.
as_user() { "$@"; }
osr_fetch_stdout() { echo "INSTALLER-FETCHED" >>"$OUT"; printf 'true\n'; }
error()   { echo "ERROR $*" >>"$OUT"; return 1; }
git() {
    echo "GIT $*" >>"$OUT"
    # `git clone --depth 1 <url> <dir>`: the destination is the last argument.
    _dst=""
    for _a in "$@"; do _dst=$_a; done
    mkdir -p "$_dst/custom" "$_dst/lib"
    printf '#omz core\n' >"$_dst/oh-my-zsh.sh"
    printf 'stock\n' >"$_dst/custom/example.zsh"
}

# --- a real install is left alone (§2) ---------------------------------------
mkdir -p "$OSR_HOME/.oh-my-zsh"
printf '#omz core\n' >"$OSR_HOME/.oh-my-zsh/oh-my-zsh.sh"
: >"$OUT"
CAP=$(install_omz 2>&1)
refute_contains "$OUT" "GIT " "existing omz: nothing cloned"
refute_contains "$OUT" "INSTALLER-FETCHED" "existing omz: the installer is not fetched"
printf '%s\n' "$CAP" | grep -q 'already installed' && ok "existing omz: says it is skipping" \
    || fail "existing omz: no skip message"

# --- the stub case: a directory with plugins but no core ---------------------
rm -rf "$OSR_HOME/.oh-my-zsh"
mkdir -p "$OSR_HOME/.oh-my-zsh/custom/plugins/zsh-syntax-highlighting"
printf 'plugin\n' >"$OSR_HOME/.oh-my-zsh/custom/plugins/zsh-syntax-highlighting/zsh-syntax-highlighting.plugin.zsh"
: >"$OUT"
install_omz >/dev/null 2>&1 || fail "install_omz failed on the stub case"
assert_contains "$OUT" "GIT clone --depth 1 https://github.com/ohmyzsh/ohmyzsh.git" \
    "stub: clones the core (the installer refuses an existing \$ZSH dir)"
refute_contains "$OUT" "INSTALLER-FETCHED" "stub: does not run upstream's installer"
[ -r "$OSR_HOME/.oh-my-zsh/oh-my-zsh.sh" ] && ok "stub: the core is now in place" \
    || fail "stub: still no oh-my-zsh.sh"
[ -f "$OSR_HOME/.oh-my-zsh/custom/plugins/zsh-syntax-highlighting/zsh-syntax-highlighting.plugin.zsh" ] \
    && ok "stub: the existing plugin clones are carried across" \
    || fail "stub: custom/plugins was lost"
[ ! -e "$OSR_HOME/.oh-my-zsh.osr-new" ] && ok "stub: the staging dir is cleaned up" \
    || fail "stub: .oh-my-zsh.osr-new left behind"

# ...and running it again is a no-op, which is the §2 contract.
: >"$OUT"
install_omz >/dev/null 2>&1
refute_contains "$OUT" "GIT " "second run: nothing cloned (idempotent)"

# --- no directory at all: upstream's unattended installer --------------------
rm -rf "$OSR_HOME/.oh-my-zsh"
: >"$OUT"
install_omz >/dev/null 2>&1 || true
assert_contains "$OUT" "INSTALLER-FETCHED" "fresh box: still takes the upstream installer"
refute_contains "$OUT" "GIT clone" "fresh box: no clone path"

finish
