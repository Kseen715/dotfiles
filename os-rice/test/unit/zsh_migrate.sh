#!/bin/sh
# Proves modules/zsh.c patches a box that was installed before the current rc.d
# layers existed. §5 keeps 00-env/99-local user-owned and seeded once, so
# install_layer can never reach them and every fix shipped there would otherwise
# apply to new machines only.
#
# The safety property is the point of the test, not the patching: an exact match
# against text os-rice itself shipped is rewritten, anything the user touched is
# reported and left exactly as it was. Additive migrations apply either way.
#
# Hermetic: OSR_HOME is a sandbox and PATH is reduced to a stub bin/, so nothing
# reaches the network, the package manager or the login shell. The module is C
# now, so it runs through the core rather than being sourced; the migrations
# themselves are lib/migrate.c's and have their own parity test.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_DOTFILES OSR_PKG=apt
NO_COLOR=1; OSR_USER=$(id -un); export OSR_USER   # as_user becomes a no-op
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/user.sh"
. "$OSR_LIB/config.sh"; . "$OSR_LIB/migrate.sh"
# build.sh for _fzf_ok/FZF_MIN: modules/zsh.sh gates the fzf version with it
# (an old distro fzf breaks the up-arrow history picker), and install.sh has
# every lib sourced by the time a module runs.
. "$OSR_LIB/net.sh"; . "$OSR_LIB/pkg.sh"; . "$OSR_LIB/build.sh"
# ...and pinned true, so the migration path under test never reaches provide_fzf:
# a CI host with an old (or no) fzf would otherwise download one.
_fzf_ok() { return 0; }
. "$HERE/../lib.sh"

OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip zsh_migrate: %s is not built\n' "$OSR_BIN"
    exit 0
fi

BIN=$(mktemp -d)
trap 'rm -rf "$BIN"' EXIT INT TERM
for _t in sh env cat grep sed printf id rm mkdir mktemp test true false tee \
          cp chmod cut tr head sort wc dirname basename; do
    _p=$(command -v "$_t" 2>/dev/null) || :
    case "$_p" in /*) ln -sf "$_p" "$BIN/$_t" ;; esac
done
# sudo logs nothing and runs nothing: no escalation in this test is wanted.
printf '#!/bin/sh\nexit 0\n' >"$BIN/sudo"
# apt says everything is installed, so the package step is a §2 no-op; a `zsh`
# and a new-enough `fzf` on PATH keep the login-shell and provider paths out of
# the way, which is what the sh version's osr_shell_is/_fzf_ok stubs did.
printf '#!/bin/sh\nexit 0\n' >"$BIN/dpkg"
printf '#!/bin/sh\nexit 0\n' >"$BIN/zsh"
printf '#!/bin/sh\nprintf "0.74.3 (15f64c49)\\n"\n' >"$BIN/fzf"
# git: oh-my-zsh and the plugins are cloned with it. A no-op git leaves the
# module's own §2 probes to decide, and nothing is fetched.
printf '#!/bin/sh\nexit 0\n' >"$BIN/git"
chmod +x "$BIN/sudo" "$BIN/dpkg" "$BIN/zsh" "$BIN/fzf" "$BIN/git"

# run_module <log> — the module, with $OSR_HOME as its sandbox.
run_module() {
    env -i PATH="$BIN" OSR_ROOT="$OSR_ROOT" OSR_LIB="$OSR_LIB" \
        OSR_DOTFILES="$OSR_DOTFILES" OSR_PKG=apt OSR_ARCH=x86_64 \
        OSR_DISTRO=ubuntu OSR_CODENAME=noble OSR_INIT=systemd \
        OSR_USER="$OSR_USER" OSR_HOME="$OSR_HOME" HOME="$OSR_HOME" \
        OSR_SHELLS_FILE="$OSR_HOME/.shells" OSR_PASSWD_FILE="$OSR_HOME/.passwd" \
        NO_COLOR=1 TERM=dumb OSR_VERBOSE=1 \
        "$OSR_BIN" module run zsh >"$1" 2>&1 || :
}

# The legacy 99-local.zsh that shells used to carry, verbatim.
legacy_local() { cat <<'EOF'
# --- ssh-agent: reuse an existing agent, or start one -------------------------
# NOTE: start_agent never writes $SSH_ENV, so the -f test below is never true and
# a fresh agent gets spawned for every shell. Moved verbatim; not fixed here.
SSH_ENV="$HOME/.ssh/agent-environment"

start_agent() {
    eval "$(ssh-agent -s)" >/dev/null
    # Only add private keys (ignore .pub, config, known_hosts, etc.)
    ssh-add ~/.ssh/* 2>/dev/null
}

if [ -f "$SSH_ENV" ]; then
    . "$SSH_ENV" >/dev/null
    kill -0 "$SSH_AGENT_PID" 2>/dev/null || start_agent
else
    start_agent
fi

# --- nvm ---------------------------------------------------------------------
# Sourced last on purpose: nvm prepends its active node dir to PATH and should
# win over the PATH edits in 00-env.zsh.
export NVM_DIR="$HOME/.nvm"
[ -s "$NVM_DIR/nvm.sh" ] && \. "$NVM_DIR/nvm.sh"
[ -s "$NVM_DIR/bash_completion" ] && \. "$NVM_DIR/bash_completion"
EOF
}

legacy_env_v1() { cat <<'EOF'
# 00-env.zsh — user/machine environment.
export EDITOR=micro
export MY_OWN_SETTING=keepme

# Homebrew shell environment (machine-specific), only if installed.
if command -v brew >/dev/null 2>&1; then
    eval "$(brew shellenv)"
fi
EOF
}

legacy_env_v2() { cat <<'EOF'
# 00-env.zsh — user/machine environment.
export MY_OWN_SETTING=keepme

if [ -z "${HOMEBREW_PREFIX:-}" ]; then
    if [ -x /home/linuxbrew/.linuxbrew/bin/brew ]; then
        eval "$(/home/linuxbrew/.linuxbrew/bin/brew shellenv)"
    elif command -v brew >/dev/null 2>&1; then
        eval "$(brew shellenv)"
    fi
fi
EOF
}

# old_install <env-fixture> — sandbox holding a pre-migration rc.d, then run the
# module over it and capture its output.
old_install() {
    OSR_HOME=$(mktemp -d); export OSR_HOME
    mkdir -p "$OSR_HOME/.config/osr/zsh/rc.d"
    "$1"          >"$OSR_HOME/.config/osr/zsh/rc.d/00-env.zsh"
    legacy_local  >"$OSR_HOME/.config/osr/zsh/rc.d/99-local.zsh"
    LOG=$(mktemp)
    # The account already uses zsh, so the login-shell path is a §2 no-op.
    printf '%s:x:1000:1000::%s:%s\n' "$OSR_USER" "$OSR_HOME" "$BIN/zsh" >"$OSR_HOME/.passwd"
    printf '%s\n' "$BIN/zsh" >"$OSR_HOME/.shells"
    run_module "$LOG"
    RC="$OSR_HOME/.config/osr/zsh/rc.d"
}

# --- 1: a stock legacy box is patched ----------------------------------------
old_install legacy_env_v1
assert_contains "$RC/00-env.zsh"   '_osr_brew'          "brew probe rewritten to absolute paths"
refute_contains "$RC/00-env.zsh"   '^if command -v brew' "the PATH-miss probe is gone"
assert_contains "$RC/00-env.zsh"   'typeset -U path'    "typeset -U appended"
assert_contains "$RC/00-env.zsh"   'MY_OWN_SETTING'     "unrelated user content preserved"
refute_contains "$RC/99-local.zsh" 'nvm\.sh'            "eager nvm source removed from 99-local"
refute_contains "$RC/99-local.zsh" 'start_agent'        "leaking ssh-agent removed from 99-local"
assert_contains "$RC/30-tools.zsh" 'unfunction nvm'     "30-tools.zsh installed to replace them"
[ -f "$RC/00-env.zsh.pre-migrate" ] && ok "pre-migrate backup kept" || fail "no pre-migrate backup"
OLD_HOME=$OSR_HOME

# --- 2: re-running is silent (idempotent) ------------------------------------
OSR_HOME=$OLD_HOME; export OSR_HOME
LOG2=$(mktemp)
run_module "$LOG2"
refute_contains "$LOG2" 'migrated'  "second run migrates nothing"
refute_contains "$LOG2" 'still has' "second run does not warn about its own fix"
rm -rf "$OLD_HOME"

# --- 3: the newer shipped brew block is recognised too ------------------------
old_install legacy_env_v2
assert_contains "$RC/00-env.zsh" '_osr_brew'            "second brew generation also migrated"
refute_contains "$RC/00-env.zsh" 'elif command -v brew' "its PATH-miss fallback is gone"
rm -rf "$OSR_HOME"

# --- 4: a hand-edited region is reported, never rewritten ---------------------
OSR_HOME=$(mktemp -d); export OSR_HOME
mkdir -p "$OSR_HOME/.config/osr/zsh/rc.d"
RC="$OSR_HOME/.config/osr/zsh/rc.d"
legacy_env_v1 | sed 's|eval "$(brew shellenv)"|eval "$(brew shellenv)"  # mine|' >"$RC/00-env.zsh"
legacy_local  | sed 's|\$HOME/\.nvm|$HOME/custom-nvm|'                          >"$RC/99-local.zsh"
BEFORE=$(md5sum <"$RC/99-local.zsh")
LOG3=$(mktemp)
printf '%s:x:1000:1000::%s:%s\n' "$OSR_USER" "$OSR_HOME" "$BIN/zsh" >"$OSR_HOME/.passwd"
printf '%s\n' "$BIN/zsh" >"$OSR_HOME/.shells"
run_module "$LOG3"
assert_contains "$LOG3" 'still has'      "an edited region is reported"
assert_contains "$RC/00-env.zsh" '# mine' "the user's edit survives untouched"
assert_eq "$BEFORE" "$(md5sum <"$RC/99-local.zsh")" "an edited 99-local is byte-identical after the run"
assert_contains "$RC/00-env.zsh" 'typeset -U path' "additive migration still applies to an edited file"
rm -rf "$OSR_HOME"

finish
