/* lib/nerdfont.h -- Nerd Font installation, the C port of lib/fonts.sh.
 *
 * Icons and glyphs are a shared cosmetic asset several modules need (foot,
 * starship, wezterm), so the download-unzip-register logic lives here once
 * instead of being pasted per module. Best-effort by contract: a font is
 * cosmetic, so every failure warns and SUCCEEDS rather than aborting a rice or
 * breaking the §2 rerun contract.
 *
 * Not lib/fonts.h: that name belongs to the Windows core's font installer,
 * which registers a face through the shell APIs rather than fontconfig. The
 * two are never linked into one binary, but the headers still may not collide.
 *
 * C89 + POSIX.
 */
#ifndef OSR_NERDFONT_H
#define OSR_NERDFONT_H

/* osr_install_nerd_font -- install a Nerd Font from ryanoasis/nerd-fonts.
 * name is NULL or "" for the default, JetBrainsMono. Idempotent: skipped when
 * a matching family is already registered with fontconfig (§2). All work runs
 * as OSR_USER (user-space, §8). Always returns 1. */
int osr_install_nerd_font(const char *name);

#endif /* OSR_NERDFONT_H */
