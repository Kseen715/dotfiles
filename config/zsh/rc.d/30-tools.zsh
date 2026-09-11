# 30-tools.zsh — developer tool shell integration. LAYER: dotfiles-owned,
# overwritten on update, rice-independent. Sourced after 00-env (so PATH edits
# here win over it) and after aliases.
#
# Everything in this file is guarded: on a machine without nvm, or without ssh,
# each block is a no-op. That is what lets it ship to every system unconditionally.
#
# This exists because both fixes below were originally in 99-local.zsh, which is
# per-machine and never populated from the repo — so a fresh install got neither.

# --- nvm ----------------------------------------------------------------------
# Do NOT source nvm.sh here. It cost ~360 ms of every shell start on a 9800X3D —
# measured with zprof, 91% of all profiled function time, dwarfing oh-my-zsh and
# compinit combined. Almost all of it is nvm_auto resolving and activating the
# default version.
#
# The only thing that eager source actually bought us is node/npm/npx on PATH,
# and that is just a directory. So put the directory there directly and defer
# nvm.sh to the first `nvm` call.
export NVM_DIR="${NVM_DIR:-$HOME/.nvm}"

() {
    emulate -L zsh
    setopt local_options numeric_glob_sort   # v26.10.0 must sort above v26.3.0
    local want= dirs=()
    [[ -r $NVM_DIR/alias/default ]] && want=$(<$NVM_DIR/alias/default)
    # Newest install matching the default alias, else newest overall. Both globs
    # are nullglob'd (N), so a missing or empty ~/.nvm leaves $path untouched.
    dirs=( $NVM_DIR/versions/node/v${want}*(N/On) )
    (( $#dirs )) || dirs=( $NVM_DIR/versions/node/*(N/On) )
    # Guard-style like the helpers in 00-env.zsh (§5 "Config idempotency"):
    # re-sourcing, or inheriting a PATH that already has this dir, must not
    # duplicate the entry. Deliberately does not rely on `typeset -U path` —
    # 00-env.zsh is seeded once, so a machine seeded before that line existed
    # never got it.
    (( $#dirs )) && [[ ${path[(Ie)$dirs[1]/bin]} -eq 0 ]] && path=( $dirs[1]/bin $path )
}

# First call swaps itself for the real nvm and re-runs the command, so `nvm use`,
# `nvm install` etc. behave normally — they just pay the ~360 ms once, in the
# shell that asked for it, instead of in every shell.
if [[ -s $NVM_DIR/nvm.sh ]]; then
    nvm() {
        unfunction nvm
        . "$NVM_DIR/nvm.sh"
        [[ -s $NVM_DIR/bash_completion ]] && . "$NVM_DIR/bash_completion"
        nvm "$@"
    }
fi

# --- ssh-agent ----------------------------------------------------------------
# Reuse an agent instead of spawning one per shell. The previous version never
# wrote $SSH_ENV, so its `[ -f "$SSH_ENV" ]` test was always false and every
# single shell started a fresh agent — 48 of them were live on this box when the
# bug was found. Writing the file is the actual fix; the rest is just ordering.
SSH_ENV="$HOME/.ssh/agent-environment"

_osr_ssh_agent_start() {
    command -v ssh-agent >/dev/null 2>&1 || return 0
    (umask 077; ssh-agent -s >| "$SSH_ENV") || return 0
    . "$SSH_ENV" >/dev/null
    # Private keys only: skip *.pub, config, known_hosts, and the agent socket dir.
    () {
        emulate -L zsh
        setopt local_options extended_glob
        local k=
        for k in "$HOME"/.ssh/*~*.(pub|old)(.N); do
            [[ $k == */config || $k == */known_hosts ]] && continue
            ssh-add "$k" 2>/dev/null
        done
    }
}

# 1. A live socket is already in the environment — desktop keyring, systemd user
#    unit, `ssh -A` forwarding, or simply a parent shell. Use it, start nothing.
#    Testing -S and not just "is the var set" matters: when an agent dies its
#    socket goes with it, and a stale SSH_AUTH_SOCK would otherwise pin every new
#    shell to an agent that is not there. -S is a stat, not a fork, so it stays
#    off the startup budget in a way `ssh-add -l` would not.
# 2. Otherwise adopt the one a previous shell recorded, if its pid is still alive.
# 3. Only if both fail, start one — and record it, which is what was missing.
if [[ ! -S ${SSH_AUTH_SOCK:-} ]]; then
    [[ -r $SSH_ENV ]] && . "$SSH_ENV" >/dev/null 2>&1
    # Not `kill -0 ${SSH_AGENT_PID:-0}`: with the var unset that expands to
    # `kill -0 0`, which signals the *current process group* and always succeeds,
    # so the agent was never started. Check the var is set before probing it.
    if [[ -z ${SSH_AGENT_PID:-} ]] || ! kill -0 "$SSH_AGENT_PID" 2>/dev/null; then
        _osr_ssh_agent_start
    fi
fi
