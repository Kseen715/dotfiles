/* lib/theme_render.h -- {{role}} template substitution + theme layer
 * resolution. C port of lib/theme.sh's _osr_theme_sed/render_theme_template
 * and windows-rice/src/theme.ps1's Expand-ThemeTemplate/Get-ThemeSource/
 * Install-ThemeLayer (the same algorithm twice already, sh and PowerShell;
 * this is the third).
 *
 * Every color role gets up to four spellings in a template, exactly as
 * documented in lib/theme.sh's _osr_theme_sed:
 *
 *   {{role}}      whatever the theme wrote (usually #rrggbb)
 *   {{role_rgb}}  rrggbb, no leading #        -- only when value IS #rrggbb
 *   {{role_dec}}  r,g,b                        -- only when value IS #rrggbb
 *   {{role_sgr}}  r;g;b                        -- only when value IS #rrggbb
 *
 * plus {{THEME}} (the theme's own name) and one substitution per
 * single-valued meta key (display, description, ...). A `{{placeholder}}`
 * the palette does not define is left in the output untouched -- same
 * "the file still lands" contract the sh/ps1 originals have.
 *
 * C89.
 */
#ifndef OSR_THEME_RENDER_H
#define OSR_THEME_RENDER_H

#include "theme_list.h"

/* osr_render_template -- render tmpl_path against palette, write the
 * result to out_path. Returns 1 on success, 0 on any I/O failure.
 */
int osr_render_template(const char *tmpl_path, const osr_theme_palette *palette, const char *out_path);

/* osr_theme_layer_source -- C port of Get-ThemeSource: resolve what should
 * be installed for <app>/<file> under <theme>, in order:
 *   1. <themes_root>/<theme>/config/<app>/<file>   (literal override --
 *      the same escape hatch install_theme_layer documents on the Linux
 *      side, for a look that isn't a palette substitution)
 *   2. render <repo_root>/<app>/<file>.tmpl against
 *      <themes_root>/<theme>/theme.list
 *
 * out_path receives either the literal file's own path, or a freshly
 * rendered temp file's path -- *is_temp tells the caller which, so it
 * knows whether to call osr_theme_layer_cleanup() afterward. Returns 1 if
 * something was resolved, 0 if this theme has neither a literal file nor
 * a renderable template for <app>/<file>.
 */
int osr_theme_layer_source(const char *themes_root, const char *repo_root,
                            const char *app, const char *file, const char *theme,
                            char *out_path, unsigned long out_path_sz, int *is_temp);

/* osr_theme_layer_cleanup -- remove the temp file osr_theme_layer_source
 * made, if it made one. Safe to call unconditionally with whatever
 * *is_temp it returned.
 */
void osr_theme_layer_cleanup(const char *path, int is_temp);

#endif /* OSR_THEME_RENDER_H */
