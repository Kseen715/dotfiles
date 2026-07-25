# modules/wezterm.sh — WezTerm terminal + Nerd/emoji fonts + dotfiles config. ONE
# copy, POSIX (was .../modules/wezterm.sh). Config is applied by the dotfiles
# repo's own wezterm/install.sh when present. Available module (foot is the
# rice's default terminal).
run_step "Installing WezTerm" pkg_install wezterm ttf-jetbrains-mono-nerd noto-fonts-emoji
if [ -x "$OSR_DOTFILES/wezterm/install.sh" ]; then
    run_step "Installing WezTerm dotfiles" as_user "$OSR_DOTFILES/wezterm/install.sh" -y
fi
