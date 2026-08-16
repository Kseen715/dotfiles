/* lib/fonts.h -- Nerd Font install, C port of windows-rice/src/fonts.ps1's
 * Install-NerdFont (the scoop/choco half only).
 *
 * NOT ported: fonts.ps1's manual GitHub-zip-download-and-register fallback
 * for a machine with neither scoop nor choco (downloads a release asset,
 * extracts it, registers each .ttf via the Shell.Application COM copy).
 * That's a real, sizable feature on its own (HTTP + zip extraction + font
 * registration) with no data source yet for "which asset for which font"
 * beyond scraping the GitHub release JSON osr_fetch_to_buffer (lib/net.h)
 * could fetch -- a known, documented gap, not a silent omission. scoop and
 * choco cover the realistic case this session's dev machine and most
 * users are in.
 *
 * C89.
 */
#ifndef OSR_FONTS_H
#define OSR_FONTS_H

/* osr_font_installed -- 1 if a font family whose name contains `name`
 * (case-insensitive substring, matches fonts.ps1's own [regex]::Escape
 * match) is already installed, else 0.
 */
int osr_font_installed(const char *name);

/* osr_install_nerd_font -- ensure the Nerd Font variant of `name` (e.g.
 * "JetBrainsMono") is installed: skip if osr_font_installed already says
 * yes, else try scoop's nerd-fonts bucket, else choco. Returns 1 on
 * success (already-installed counts as success), 0 if neither manager is
 * available or both attempts failed.
 */
int osr_install_nerd_font(const char *name);

#endif /* OSR_FONTS_H */
