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

FONT_VERSION=v3.4.0
FONT_NAME=JetBrainsMono
FONT_URL="https://github.com/ryanoasis/nerd-fonts/releases/download/$FONT_VERSION/$FONT_NAME.zip"

# osr_install_nerd_font — idempotent + best-effort. Skips when the font is
# already registered; on any download/unzip failure it warns and returns 0 so a
# cosmetic asset never aborts the rice or breaks the §2 rerun contract.
osr_install_nerd_font() {
    if command -v fc-list >/dev/null 2>&1 && as_user fc-list 2>/dev/null | grep -qi "JetBrainsMono.*Nerd"; then
        info "JetBrains Mono Nerd Font already installed — skipping"
        return 0
    fi
    if ! command -v unzip >/dev/null 2>&1; then
        warn "unzip not available — skipping Nerd Font install"
        return 0
    fi
    _ff_dir="$OSR_HOME/.local/share/fonts"
    _ff_zip="${TMPDIR:-/tmp}/$FONT_NAME-$$.zip"
    as_user mkdir -p "$_ff_dir"
    if ! osr_download "$FONT_URL" "$_ff_zip"; then
        warn "failed to download Nerd Font ($FONT_URL) — skipping"
        rm -f "$_ff_zip"
        return 0
    fi
    if ! as_user unzip -o "$_ff_zip" -d "$_ff_dir" >/dev/null 2>&1; then
        warn "failed to unzip Nerd Font — skipping"
        rm -f "$_ff_zip"
        return 0
    fi
    rm -f "$_ff_zip"
    command -v fc-cache >/dev/null 2>&1 && as_user fc-cache -f "$_ff_dir" >/dev/null 2>&1
    return 0
}

run_step "Installing foot terminal" pkg_install foot unzip fontconfig
run_step "Installing JetBrains Mono Nerd Font" osr_install_nerd_font

# Base config (dotfiles-owned, overwrite-on-update §5).
install_layer "$OSR_DOTFILES/foot/foot.ini" "$OSR_HOME/.config/foot/foot.ini"

# Palette (rice-owned theme, swapped on switch §6). Rice override wins; the
# dotfiles default covers a rice that ships no palette.
if [ -f "$OSR_RICE_DIR/config/foot/foot-colors.ini" ]; then
    install_layer "$OSR_RICE_DIR/config/foot/foot-colors.ini" "$OSR_HOME/.config/foot/foot-colors.ini"
elif [ -f "$OSR_DOTFILES/foot/foot-colors.ini" ]; then
    install_layer "$OSR_DOTFILES/foot/foot-colors.ini" "$OSR_HOME/.config/foot/foot-colors.ini"
fi
