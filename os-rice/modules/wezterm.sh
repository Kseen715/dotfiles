# modules/wezterm.sh — WezTerm terminal + Nerd/emoji fonts + dotfiles config. ONE
# copy, POSIX. `.wezterm.lua` is dotfiles-owned config (§5), installed via the
# layered install_layer helper (was: shelling out to the legacy wezterm/install.sh).
# foot is the rice's default terminal; wezterm is an available module.
run_step "Installing WezTerm" pkg_install wezterm ttf-jetbrains-mono-nerd noto-fonts-emoji
run_step "Installing JetBrains Mono Nerd Font" osr_install_nerd_font JetBrainsMono

if [ -f "$OSR_DOTFILES/wezterm/.wezterm.lua" ]; then
    install_layer "$OSR_DOTFILES/wezterm/.wezterm.lua" "$OSR_HOME/.wezterm.lua"
fi
