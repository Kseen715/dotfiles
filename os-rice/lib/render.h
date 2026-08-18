/* lib/render.h -- theme templates, rendered.
 *
 * A theme is a palette, not a directory of app configs: an app ships ONE
 * `<file>.tmpl` carrying `{{key}}` placeholders, and every theme fills them in
 * (§6b). lib/config.sh does it by generating a sed script from the theme and
 * running `sed -f`; this is the same substitution set applied directly, for
 * modules written in C.
 *
 * C89 + POSIX.
 */
#ifndef OSR_RENDER_H
#define OSR_RENDER_H

#include "common.h"

/* One `{{key}}` -> value substitution. */
typedef struct {
    Str from;
    Str to;
} Rule;

typedef struct {
    Rule *items;
    size_t count;
    size_t cap;
} Rules;

/* osr_theme_rules -- every substitution a theme defines, in the order the sh
 * version's sed script listed them (which matters: a rule can rewrite what an
 * earlier one produced). drop_final_newline, when not NULL, reports the
 * manifest quirk `osr theme sed` has to reproduce. */
void osr_theme_rules(Rules *out, const char *theme, int *drop_final_newline);
void osr_theme_rules_free(Rules *r);

/* osr_render_template -- write src to dst with the theme's substitutions
 * applied, warning about any `{{key}}` the theme did not define -- except
 * {{WALLPAPER_PATH}}, which a second pass fills in (§6). Returns 1 on
 * success. */
int osr_render_template(const char *src, const char *dst, const char *theme);

/* osr_theme_source -- where this theme's version of <app>/<name> comes from:
 * the theme's own config/ dir when it ships the file, else the dotfiles
 * template rendered into a temp file (*is_temp says which, so the caller can
 * remove it). Returns 0 when the theme has neither. */
int osr_theme_source(Str *out, const char *app, const char *name, int *is_temp);

#endif /* OSR_RENDER_H */
