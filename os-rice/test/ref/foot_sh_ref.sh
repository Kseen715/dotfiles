# test/ref/foot_sh_ref.sh — the sh implementation of modules/foot.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/foot.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: wayland
# themable: yes
# modules/foot.sh — foot terminal + JetBrains Mono Nerd Font + layered config.
# ONE copy, POSIX, distro-agnostic (was linux-rhel/modules/foot.sh, bash). The
# package goes through pkg_install/pkgmap; the font is a best-effort cosmetic
# asset (warn, never fail a run); config is split by ownership (§5):
#
#   foot.ini          dotfiles-owned (10-layer) — overwritten on update
#   foot-colors.ini   rice-owned theme (90-layer) — swapped on rice switch (§6),
#                     falling back to the dotfiles default when a rice ships none
#
# foot.ini carries `include=~/.config/foot/foot-colors.ini`, so the palette layer
# swaps independently of the base config — the §5 split applied to a DE config.
#
# The Nerd Font install is the shared, best-effort lib/fonts.sh helper (also used
# by starship/wezterm) — one copy of the download-unzip-register logic.

run_step "Installing foot terminal" pkg_install foot unzip fontconfig
run_step "Installing JetBrains Mono Nerd Font" osr_install_nerd_font JetBrainsMono

# Base config (dotfiles-owned, overwrite-on-update §5).
install_layer "$OSR_DOTFILES/foot/foot.ini" "$OSR_HOME/.config/foot/foot.ini"

# Palette (rice-owned theme, swapped on switch §6). Rice override wins; the
# dotfiles default covers a rice that ships no palette. install_foot_palette,
# not install_layer: the palette section was renamed in foot 1.26, so the file
# is adapted to the foot that was just installed.
_foot_pal=$(osr_theme_source foot foot-colors.ini) || _foot_pal=""
if [ -n "$_foot_pal" ]; then
    install_foot_palette "$_foot_pal" "$OSR_HOME/.config/foot/foot-colors.ini"
    case "$_foot_pal" in "${TMPDIR:-/tmp}"/osr-theme-*) rm -f "$_foot_pal" ;; esac
elif [ -f "$OSR_DOTFILES/foot/foot-colors.ini" ]; then
    install_foot_palette "$OSR_DOTFILES/foot/foot-colors.ini" "$OSR_HOME/.config/foot/foot-colors.ini"
fi
