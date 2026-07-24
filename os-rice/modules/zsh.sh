# modules/zsh.sh — zsh + prompt + layered rc.d config. ONE copy, POSIX,
# distro-agnostic: the package line goes through pkg_install/pkgmap, everything
# else is shared (§Module example). Sourced by install.sh with OSR_* in scope.

run_step "Installing zsh and tools" pkg_install zsh git curl lsd starship

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

# rice-owned prompt theme + starship config, swapped on rice switch (§6).
if [ -f "$OSR_RICE_DIR/config/zsh/90-theme.zsh" ]; then
    install_layer "$OSR_RICE_DIR/config/zsh/90-theme.zsh" "$OSR_RCDIR/90-theme.zsh"
fi
if [ -f "$OSR_RICE_DIR/config/starship.toml" ]; then
    install_layer "$OSR_RICE_DIR/config/starship.toml" "$OSR_HOME/.config/starship.toml"
fi

seed_empty "$OSR_RCDIR/99-local.zsh"

# Thin loader: own only a marked block in ~/.zshrc (§5).
install_zsh_loader "$OSR_RCDIR" "$OSR_HOME/.zshrc"

# Default login shell -> zsh, only when it isn't already (§2). chsh is absent on
# busybox/Alpine — skip gracefully there rather than failing the run.
_zsh_bin=$(command -v zsh || true)
if [ -z "$_zsh_bin" ]; then
    :
elif ! command -v chsh >/dev/null 2>&1; then
    warn "chsh not available - leaving login shell unchanged (set it manually)"
elif [ "$(osr_user_shell "$OSR_USER")" != "$_zsh_bin" ]; then
    run_step "Setting default shell to zsh" as_root chsh -s "$_zsh_bin" "$OSR_USER"
fi
