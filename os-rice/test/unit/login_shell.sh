#!/bin/sh
# Proves the login-shell change actually lands on every kind of box: chsh
# (util-linux/shadow), usermod when chsh is missing (minimal Fedora) or refuses,
# and a direct /etc/passwd rewrite when the image has neither (busybox/Alpine).
# Also proves the §2 guard: /etc/shells gains one entry, a shell that is already
# zsh (even under an aliased /bin -> /usr/bin path) is left alone, and a box
# where nothing works warns instead of killing the run.
#
# Hermetic: lib/user.sh runs from a copy whose /etc paths point into a sandbox,
# PATH is reduced to a fake bin so "does chsh exist" is a property of the
# scenario, and as_root only executes the file-touching commands.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB
NO_COLOR=1
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
OUT="$TMP/out"
SYS="$TMP/sys"; mkdir -p "$SYS/etc"
BIN="$TMP/bin"; mkdir -p "$BIN"

PASSWD="$SYS/etc/passwd"; SHELLS="$SYS/etc/shells"
FAKE_PASSWD="$PASSWD"; export FAKE_PASSWD
# The reading half of the user model is C now (lib/user.c), so the sandbox
# is declared rather than sed-substituted into a copy of the lib: these two
# names point both halves at the fake /etc, and OSR_PASSWD_FILE also turns NSS
# off so "tester" is answered from the sandbox and not from this machine.
OSR_PASSWD_FILE="$PASSWD"; OSR_SHELLS_FILE="$SHELLS"
export OSR_PASSWD_FILE OSR_SHELLS_FILE

# lib/user.sh writes system files by absolute path (§5a); under test it runs
# from a copy rebased into the sandbox, so a wrong mock can't reach the real
# /etc/passwd. `getent` is deliberately left out of $BIN so osr_passwd takes its
# busybox fallback and reads the sandbox file instead of the real NSS database.
USERLIB="$TMP/user.sh"
sed "s#/etc/passwd#$PASSWD#g; s#/etc/shells#$SHELLS#g" "$OSR_LIB/user.sh" >"$USERLIB"

# Real tools the lib needs, and nothing else - PATH becomes exactly $BIN so
# `command -v chsh` answers what the scenario says, not what this machine has.
for _t in grep cut awk cp rm mktemp readlink cat tee sh; do
    _p=$(command -v "$_t") && ln -sf "$_p" "$BIN/$_t"
done
: >"$BIN/zsh"; chmod +x "$BIN/zsh"          # the "installed" zsh, /bin/zsh below

# --- fakes: chsh / usermod, each present only when a scenario links it in -----
make_fake() {   # make_fake <name> <exit-code> - rewrites field 7 when it "works"
    cat >"$TMP/$1.impl" <<EOF
#!/bin/sh
[ "\$1" = -s ] || exit 2
[ $2 -eq 0 ] || exit $2
awk -F: -v OFS=: -v u="\$3" -v s="\$2" '\$1==u{\$7=s}{print}' "\$FAKE_PASSWD" >"\$FAKE_PASSWD.n"
cp -f "\$FAKE_PASSWD.n" "\$FAKE_PASSWD"
EOF
    chmod +x "$TMP/$1.impl"
}

# scenario <passwd-shell> <tools...> - fresh sandbox with only <tools> installed.
# Re-sources the lib (it is the code under test), then re-applies the mocks it
# just overwrote: as_root must never escalate here, only touch sandbox files.
scenario() {
    _sh=$1; shift
    : >"$OUT"
    printf 'root:x:0:0:root:/root:/bin/sh\ntester:x:1000:1000::/home/tester:%s\n' "$_sh" >"$PASSWD"
    printf '/bin/sh\n/bin/bash\n' >"$SHELLS"
    rm -f "$BIN/chsh" "$BIN/usermod"
    for _t in "$@"; do cp "$TMP/$_t.impl" "$BIN/$_t"; done
    OSR_USER=tester
    . "$USERLIB"
    mocks
}

mocks() {
    as_root() {
        echo "ROOT $*" >>"$OUT"
        _cmd=$1; shift
        case "$_cmd" in
            cp)            cp "$@" ;;
            tee)           if [ "$1" = -a ]; then shift; cat >>"$1"; else cat >"$1"; fi ;;
            chsh|usermod)  "$_cmd" "$@" ;;
        esac
    }
    # Scenario 7 drives the module, not the lib: record the call instead.
    if [ -n "${STUB_SETSHELL:-}" ]; then
        osr_set_login_shell() { echo "SETSHELL $*" >>"$OUT"; return "${SETSHELL_RC:-0}"; }
    fi
}

# run <cmd...> - execute with PATH reduced to the sandbox, restore it after.
run() {
    _op=$PATH; PATH=$BIN
    if "$@"; then _rc=0; else _rc=$?; fi
    PATH=$_op
    return "$_rc"
}

make_fake chsh 0
make_fake usermod 0
make_fake chsh_broken 1

# --- 1. chsh present: the ordinary path --------------------------------------
scenario /bin/sh chsh usermod
if run osr_set_login_shell tester "$BIN/zsh"; then ok "chsh box: reports success"
else fail "chsh box: reported failure"; fi
assert_eq "$BIN/zsh" "$(run osr_user_shell tester)" "chsh box: passwd now says zsh"
assert_contains "$OUT" "ROOT chsh -s $BIN/zsh tester" "chsh box: used chsh"
refute_contains "$OUT" "ROOT usermod" "chsh box: no pointless usermod fallback"
assert_contains "$SHELLS" "^$BIN/zsh\$" "chsh box: zsh registered in /etc/shells"

# --- 2. no chsh (minimal Fedora): usermod picks it up ------------------------
scenario /bin/sh usermod
if run osr_set_login_shell tester "$BIN/zsh"; then ok "usermod box: reports success"
else fail "usermod box: reported failure"; fi
assert_eq "$BIN/zsh" "$(run osr_user_shell tester)" "usermod box: passwd now says zsh"
assert_contains "$OUT" "ROOT usermod -s $BIN/zsh tester" "usermod box: used usermod"

# --- 3. chsh present but refuses: still ends up set --------------------------
scenario /bin/sh usermod
cp "$TMP/chsh_broken.impl" "$BIN/chsh"
if run osr_set_login_shell tester "$BIN/zsh"; then ok "failing chsh: reports success"
else fail "failing chsh: reported failure"; fi
assert_eq "$BIN/zsh" "$(run osr_user_shell tester)" "failing chsh: usermod finished the job"

# --- 4. busybox/Alpine: neither tool -> direct /etc/passwd rewrite -----------
scenario /bin/sh
if run osr_set_login_shell tester "$BIN/zsh"; then ok "busybox box: reports success"
else fail "busybox box: reported failure"; fi
assert_eq "$BIN/zsh" "$(run osr_user_shell tester)" "busybox box: passwd rewritten in place"
assert_eq "root:x:0:0:root:/root:/bin/sh" "$(head -n 1 "$PASSWD")" "busybox box: other users untouched"

# --- 5. rerun (§2): already zsh, and aliased /bin vs /usr/bin paths ----------
mkdir -p "$SYS/usr/bin"; ln -sf "$BIN/zsh" "$SYS/usr/bin/zsh"
scenario "$BIN/zsh"
if run osr_shell_is tester "$SYS/usr/bin/zsh"; then
    ok "rerun: /bin/zsh vs /usr/bin/zsh recognised as the same shell"
else
    fail "rerun: aliased zsh path treated as a different shell"
fi
run osr_set_login_shell tester "$BIN/zsh"
assert_eq 1 "$(grep -c "^$BIN/zsh\$" "$SHELLS")" "rerun: /etc/shells not duplicated"

# --- 6. nothing works -> non-zero, so the caller can warn --------------------
scenario /bin/sh
printf 'root:x:0:0:root:/root:/bin/sh\n' >"$PASSWD"     # target user not in passwd
if run osr_set_login_shell tester "$BIN/zsh"; then
    fail "unknown user: claimed success"
else
    ok "unknown user: reports failure instead of pretending"
fi

# --- 7. modules/zsh.sh wiring: guard, call, and the non-fatal failure --------
run_step() { shift; "$@"; }
for _f in pkg_install install_omz install_zsh_plugin seed_once install_layer \
          seed_empty install_zsh_loader; do
    eval "$_f() { :; }"
done
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd); export OSR_DOTFILES
OSR_HOME="$TMP/home"; export OSR_HOME OSR_USER=tester
STUB_SETSHELL=1

scenario /bin/sh
PATH="$BIN:$PATH" . "$OSR_ROOT/modules/zsh.sh" >>"$OUT" 2>&1
assert_contains "$OUT" "SETSHELL tester $BIN/zsh" "module: sets the login shell when it is /bin/sh"

scenario "$BIN/zsh"
PATH="$BIN:$PATH" . "$OSR_ROOT/modules/zsh.sh" >>"$OUT" 2>&1
refute_contains "$OUT" "SETSHELL" "module: skips when the shell is already zsh (§2)"

scenario /bin/sh
SETSHELL_RC=1
PATH="$BIN:$PATH" . "$OSR_ROOT/modules/zsh.sh" >>"$OUT" 2>&1
assert_contains "$OUT" "could not set the login shell" "module: warns (not fatal) when no mechanism works"

finish
