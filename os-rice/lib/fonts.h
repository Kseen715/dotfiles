/* lib/fonts.h -- Nerd Font installation, the C port of lib/fonts.sh.
 *
 * Icons and glyphs are a shared cosmetic asset several modules need (foot,
 * starship, wezterm, oh-my-posh), so the logic lives in one unit rather than
 * being pasted per module.
 *
 * BEST-EFFORT BY CONTRACT, on both systems: a font is cosmetic, so every
 * failure warns and lets the module carry on rather than aborting a rice or
 * breaking the rerun contract (DESIGN section 2). Idempotent: an already
 * registered family is a skip, and a skip is a success.
 *
 * lib/fonts.c holds the two bodies. They differ more than most, because
 * "install a font" is two different acts: a directory of files plus an index
 * on POSIX, a registered object handed to a package manager on Windows. What
 * a caller sees is the same one call either way.
 *
 * C89 + POSIX, and C89 + Win32.
 */
#ifndef OSR_FONTS_H
#define OSR_FONTS_H

/* osr_install_nerd_font -- ensure the Nerd Font variant of `name` (e.g.
 * "JetBrainsMono") is installed. NULL or "" means the default, JetBrainsMono.
 *
 * POSIX: fetch the release zip from ryanoasis/nerd-fonts, unpack it into the
 * user's font directory and refresh fontconfig, all as OSR_USER (user-space,
 * section 8). Always returns 1 -- see the best-effort contract above.
 *
 * Windows: hand it to scoop's nerd-fonts bucket, else to choco. Returns 1 on
 * success (already-installed counts), 0 when neither manager is available or
 * both attempts failed.
 */
int osr_install_nerd_font(const char *name);

/* osr_font_installed -- is a font family whose name CONTAINS `name` already
 * registered? A substring match, case-insensitively, because the installed
 * family carries decoration the caller does not know about ("JetBrainsMono
 * Nerd Font Mono"). Windows only: the POSIX body asks fontconfig from inside
 * osr_install_nerd_font and has no reason to publish the probe. */
#ifdef _WIN32
int osr_font_installed(const char *name);
#endif

#endif /* OSR_FONTS_H */
