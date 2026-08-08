# session: x11+wayland
# modules/zsh.sh — zsh + prompt + layered rc.d config. ONE copy, POSIX,
# distro-agnostic: the package line goes through pkg_install/pkgmap, everything
# else is shared (§Module example). Sourced by install.sh with OSR_* in scope.

# starship (prompt) + its Nerd Font + starship.toml theme live in modules/starship.sh
# so manifest order lists starship before zsh. zsh only wires the prompt in via
# its rice-owned 90-theme.zsh (`eval "$(starship init zsh)"`).
run_step "Installing zsh and tools" pkg_install zsh git curl lsd

run_step "Installing oh-my-zsh" install_omz
run_step "Installing zsh-autosuggestions" \
    install_zsh_plugin zsh-autosuggestions https://github.com/zsh-users/zsh-autosuggestions
run_step "Installing zsh-syntax-highlighting" \
    install_zsh_plugin zsh-syntax-highlighting https://github.com/zsh-users/zsh-syntax-highlighting

# Layered rc.d config (§5): os-rice writes only what it owns.
OSR_RCDIR="$OSR_HOME/.config/osr/zsh/rc.d"
seed_once     "$OSR_DOTFILES/zsh/rc.d/00-env.zsh"     "$OSR_RCDIR/00-env.zsh"
install_layer "$OSR_DOTFILES/zsh/rc.d/10-omz.zsh"     "$OSR_RCDIR/10-omz.zsh"
install_layer "$OSR_DOTFILES/zsh/rc.d/20-aliases.zsh" "$OSR_RCDIR/20-aliases.zsh"

# rice-owned prompt theme, swapped on rice switch (§6). starship.toml is owned by
# modules/starship.sh (G5), not here.
if [ -n "${OSR_RICE_DIR:-}" ] && [ -f "$OSR_RICE_DIR/config/zsh/90-theme.zsh" ]; then
    install_layer "$OSR_RICE_DIR/config/zsh/90-theme.zsh" "$OSR_RCDIR/90-theme.zsh"
fi

seed_empty "$OSR_RCDIR/99-local.zsh"

# Thin loader: own only a marked block in ~/.zshrc (§5).
install_zsh_loader "$OSR_RCDIR" "$OSR_HOME/.zshrc"

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
