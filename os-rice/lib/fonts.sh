# lib/fonts.sh — Nerd Font installation (POSIX sh)
#
# Icons/glyphs are a shared cosmetic asset several modules need (foot, starship,
# wezterm), so the download-unzip-register logic lives here once instead of being
# pasted per module (the DRY move the whole design rests on). Best-effort by
# contract: a font is cosmetic, so every failure warns and returns 0 rather than
# aborting a rice or breaking the §2 rerun contract.

OSR_NERD_FONT_VERSION=${OSR_NERD_FONT_VERSION:-v3.4.0}

# osr_install_nerd_font [font-name] — install a Nerd Font from ryanoasis/nerd-fonts.
# Defaults to JetBrainsMono. Idempotent: skips when a matching family is already
# registered with fontconfig (§2). All work runs as OSR_USER (user-space, §8).
osr_install_nerd_font() {
    _nf_name=${1:-JetBrainsMono}
    _nf_url="https://github.com/ryanoasis/nerd-fonts/releases/download/$OSR_NERD_FONT_VERSION/$_nf_name.zip"

    if command -v fc-list >/dev/null 2>&1 && as_user fc-list 2>/dev/null | grep -qi "$_nf_name.*Nerd"; then
        info "$_nf_name Nerd Font already installed - skipping"
        return 0
    fi
    if ! command -v unzip >/dev/null 2>&1; then
        warn "unzip not available - skipping $_nf_name Nerd Font install"
        return 0
    fi
    _nf_dir="$OSR_HOME/.local/share/fonts"
    _nf_zip="${TMPDIR:-/tmp}/$_nf_name-$$.zip"
    as_user mkdir -p "$_nf_dir"
    if ! osr_download "$_nf_url" "$_nf_zip"; then
        warn "failed to download $_nf_name Nerd Font ($_nf_url) - skipping"
        rm -f "$_nf_zip"
        return 0
    fi
    if ! as_user unzip -o "$_nf_zip" -d "$_nf_dir" >/dev/null 2>&1; then
        warn "failed to unzip $_nf_name Nerd Font - skipping"
        rm -f "$_nf_zip"
        return 0
    fi
    rm -f "$_nf_zip"
    command -v fc-cache >/dev/null 2>&1 && as_user fc-cache -f "$_nf_dir" >/dev/null 2>&1
    return 0
}
