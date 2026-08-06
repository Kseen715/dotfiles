# session: x11+wayland
# modules/starship.sh — Starship prompt + Nerd Font glyphs + rice-owned theme.
# ONE copy, POSIX, distro-agnostic. Split out of zsh.sh so `osr module starship`
# installs the prompt, the icons it renders, AND a rice's starship.toml theme in
# one shot (G5: starship.toml is config, not program data).
#
#   package        native where available, script: fallback on Fedora/old Ubuntu
#                  (see any.map / dnf.map / apt.map)
#   Nerd Font      the glyphs the prompt's icons need (shared lib/fonts.sh)
#   starship.toml  the SHARED dotfiles base (starship/starship.toml) with only the
#                  color palette swapped per rice. Composed, not layered, because
#                  starship.toml has no include: base body + the rice's
#                  starship.palette.toml [palettes.theme] table (§5/§6). A rice
#                  that ships no palette gets the base's own default palette.
#
# The prompt is wired into the shell by zsh's rice-owned 90-theme.zsh
# (`eval "$(starship init zsh)"`), so manifest order lists starship before zsh.

run_step "Installing Starship prompt" pkg_install starship
run_step "Installing JetBrains Mono Nerd Font" osr_install_nerd_font JetBrainsMono

# Compose base + rice palette (§6). Standalone `osr module starship` composes the
# palette of whichever rice was picked (--theme / interactive / default).
_ss_base="$OSR_DOTFILES/starship/starship.toml"
_ss_dst="$OSR_HOME/.config/starship.toml"
if [ -f "$_ss_base" ]; then
    if [ -n "${OSR_RICE_DIR:-}" ] && [ -f "$OSR_RICE_DIR/config/starship.palette.toml" ]; then
        compose_starship_config "$_ss_base" "$OSR_RICE_DIR/config/starship.palette.toml" "$_ss_dst"
    else
        # No rice palette -> install the base as-is (its default palette).
        install_layer "$_ss_base" "$_ss_dst"
    fi
fi
