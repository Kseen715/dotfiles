# session: x11+wayland
# themable: yes
# modules/zsh.sh — zsh + prompt + layered rc.d config. ONE copy, POSIX,
# distro-agnostic: the package line goes through pkg_install/pkgmap, everything
# else is shared (§Module example). Sourced by install.sh with OSR_* in scope.

# starship (prompt) + its Nerd Font + starship.toml theme live in modules/starship.sh
# so manifest order lists starship before zsh. zsh only wires the prompt in via
# its rice-owned 90-theme.zsh (`eval "$(starship init zsh)"`).
# fzf backs the ↑ history picker in zsh/rc.d/10-omz.zsh (ten rows on screen, the
# whole history behind them — complist cannot window a list, fzf can). The rc
# side is guarded on the binary being present, so a machine without it falls
# back to the plain completion menu rather than breaking ↑; it is listed here so
# that machine does not exist. No pkgmap row: `fzf` is the package name on apt,
# pacman, dnf, apk and xbps, and resolves to a unique atom on portage.
run_step "Installing zsh and tools" pkg_install zsh git curl lsd fzf

run_step "Installing oh-my-zsh" install_omz
run_step "Installing zsh-autosuggestions" \
    install_zsh_plugin zsh-autosuggestions https://github.com/zsh-users/zsh-autosuggestions
run_step "Installing zsh-syntax-highlighting" \
    install_zsh_plugin zsh-syntax-highlighting https://github.com/zsh-users/zsh-syntax-highlighting
# Live prediction dropdown (PSReadLine ListView equivalent). Pure zsh, no
# binary, so it needs no pkgmap row. Load order is load-bearing — see the
# plugins array in zsh/rc.d/10-omz.zsh.
run_step "Installing zsh-autocomplete" \
    install_zsh_plugin zsh-autocomplete https://github.com/marlonrichert/zsh-autocomplete

# Layered rc.d config (§5): os-rice writes only what it owns.
OSR_RCDIR="$OSR_HOME/.config/osr/zsh/rc.d"
seed_once     "$OSR_DOTFILES/zsh/rc.d/00-env.zsh"     "$OSR_RCDIR/00-env.zsh"
install_layer "$OSR_DOTFILES/zsh/rc.d/10-omz.zsh"     "$OSR_RCDIR/10-omz.zsh"
install_layer "$OSR_DOTFILES/zsh/rc.d/20-aliases.zsh" "$OSR_RCDIR/20-aliases.zsh"
# 30-tools: lazy nvm + a reused ssh-agent. Dotfiles-owned rather than 99-local so
# a fresh install gets it; every block inside is guarded, so it is a no-op on a
# machine with neither installed.
install_layer "$OSR_DOTFILES/zsh/rc.d/30-tools.zsh"   "$OSR_RCDIR/30-tools.zsh"

# rice-owned prompt theme, swapped on rice switch (§6). starship.toml is owned by
# modules/starship.sh (G5), not here.
install_theme_layer zsh 90-theme.zsh "$OSR_RCDIR/90-theme.zsh" || :

seed_empty "$OSR_RCDIR/99-local.zsh"

# --- migrations for boxes installed before the above existed (lib/migrate.sh) --
#
# seed_once/seed_empty deliberately skip a file that is already there, so none of
# the fixes below would ever reach an existing machine. Each one is either purely
# additive or an exact match against text os-rice itself shipped; anything the
# user has edited is reported by migrate_stale instead of rewritten.

# The legacy 99-local.zsh that ~/.zshrc used to carry. Sourcing nvm.sh eagerly
# cost ~360 ms per shell, and start_agent never wrote $SSH_ENV so every shell
# leaked an agent (48 were live on the box where this was found). Both now live in
# 30-tools.zsh — and because 99-local loads LAST, leaving this behind would not
# just be slow, it would override the new layer entirely.
_zsh_mig_local_old() { cat <<'MIGEOF'
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
MIGEOF
}

# Two shipped generations of the brew probe, both ending in `command -v brew`.
# A PATH lookup that MISSES stats every entry, and under WSL interop $PATH
# carries ~25 /mnt/c dirs: 44.5 ms per shell on a box with no brew installed.
_zsh_mig_brew_v1_old() { cat <<'MIGEOF'
# Homebrew shell environment (machine-specific), only if installed.
if command -v brew >/dev/null 2>&1; then
    eval "$(brew shellenv)"
fi
MIGEOF
}

_zsh_mig_brew_v2_old() { cat <<'MIGEOF'
if [ -z "${HOMEBREW_PREFIX:-}" ]; then
    if [ -x /home/linuxbrew/.linuxbrew/bin/brew ]; then
        eval "$(/home/linuxbrew/.linuxbrew/bin/brew shellenv)"
    elif command -v brew >/dev/null 2>&1; then
        eval "$(brew shellenv)"
    fi
fi
MIGEOF
}

_zsh_mig_brew_new() { cat <<'MIGEOF'
# Homebrew shell environment (machine-specific), only if installed. Probed by
# absolute path, never `command -v brew`: a PATH lookup that MISSES has to stat
# every entry, and under WSL interop that is ~25 /mnt/c dirs (44.5 ms measured).
# An install outside these three prefixes needs HOMEBREW_PREFIX exported.
if [ -z "${HOMEBREW_PREFIX:-}" ]; then
    for _osr_brew in /home/linuxbrew/.linuxbrew/bin/brew /opt/homebrew/bin/brew /usr/local/bin/brew; do
        [ -x "$_osr_brew" ] || continue
        eval "$("$_osr_brew" shellenv)"
        break
    done
    unset _osr_brew
fi
MIGEOF
}

_zsh_mig_nothing() { :; }

migrate_zsh_layers() {
    # 1. Drop the legacy tool config now owned by 30-tools.zsh.
    migrate_replace "$OSR_RCDIR/99-local.zsh" "legacy nvm/ssh-agent -> 30-tools.zsh" \
        _zsh_mig_local_old _zsh_mig_nothing \
        || migrate_stale "$OSR_RCDIR/99-local.zsh" 'NVM_DIR/nvm\.sh' \
               "an eager nvm.sh source (~360 ms/shell, and it overrides 30-tools.zsh)"

    # 2. Absolute-path brew probe, whichever generation is on disk.
    migrate_replace "$OSR_RCDIR/00-env.zsh" "brew probe -> absolute path" \
        _zsh_mig_brew_v1_old _zsh_mig_brew_new \
        || migrate_replace "$OSR_RCDIR/00-env.zsh" "brew probe -> absolute path" \
               _zsh_mig_brew_v2_old _zsh_mig_brew_new \
        || migrate_stale "$OSR_RCDIR/00-env.zsh" 'command -v brew' \
               "a \`command -v brew\` PATH probe (44.5 ms/shell under WSL)"

    # 3. Additive, so it needs no exact match: without it PATH accumulates
    #    duplicates from anything that prepends unconditionally later.
    migrate_append "$OSR_RCDIR/00-env.zsh" 'typeset -U path' "typeset -U path PATH" <<'MIGEOF'
# Keep $path unique for good. The guards above only cover this file; anything
# that prepends unconditionally later (brew shellenv, /etc/profile) would still
# duplicate. Guarded so a POSIX sh sourcing this file still works.
[ -n "${ZSH_VERSION:-}" ] && typeset -U path PATH
MIGEOF
}

run_step "Migrating pre-existing zsh layers" migrate_zsh_layers

# Thin loader: own only a marked block in ~/.zshrc (§5).
install_zsh_loader "$OSR_RCDIR" "$OSR_HOME/.zshrc"

# ...and a marked block in ~/.zshenv, which is the only file early enough to
# suppress Ubuntu's duplicate global compinit (82 ms). See install_zsh_zshenv.
install_zsh_zshenv "$OSR_HOME/.zshenv"

# Default login shell -> zsh, only when it isn't already (§2). No package
# manager does this for us, and chsh is not everywhere (no busybox applet, and
# a minimal Fedora keeps it in util-linux-user), so osr_set_login_shell walks
# chsh -> usermod -> /etc/passwd and registers zsh in /etc/shells first. A box
# where all three fail warns instead of killing an otherwise good run.
set_zsh_login_shell() {
    osr_set_login_shell "$OSR_USER" "$_zsh_bin" && return 0
    warn "could not set the login shell - run: chsh -s $_zsh_bin $OSR_USER"
}

_zsh_bin=$(command -v zsh || true)
if [ -z "$_zsh_bin" ]; then
    warn "zsh not on PATH after install - leaving login shell unchanged"
elif ! osr_shell_is "$OSR_USER" "$_zsh_bin"; then
    run_step "Setting default shell to zsh" set_zsh_login_shell
fi
