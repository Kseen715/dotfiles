/* lib/theme_list.h -- theme.list parser, C port of lib/theme.sh's
 * _osr_theme_lines/osr_theme_meta/osr_theme_color/osr_theme_configs (and
 * windows-rice/src/theme.ps1's Get-ThemeListLines/Get-ThemePalette, which
 * is the same algorithm in PowerShell reading the very same files).
 *
 * theme.list is `key: value` shaped, same comment rule as rice.list except
 * narrower: a palette value IS a hash (`#rrggbb`), so only a `#` that
 * starts the line, or one with whitespace on BOTH sides, counts as a
 * comment -- `#rrggbb` never has a space after the `#`, so the two can
 * never collide. Three directive shapes:
 *
 *   display: Nord              single-valued meta (display, description,
 *                               polarity, session, ...)
 *   color: background #2e3440  multi-valued, keyed by role
 *   config: btop alacritty     multi-valued, space-joined app-dir list
 *
 * C89.
 */
#ifndef OSR_THEME_LIST_H
#define OSR_THEME_LIST_H

#define OSR_THEME_MAX_COLORS  40
#define OSR_THEME_MAX_META    16
#define OSR_THEME_NAME_LEN    64
#define OSR_THEME_VALUE_LEN   256
#define OSR_THEME_CONFIGS_LEN 1024

typedef struct {
    char name[OSR_THEME_NAME_LEN]; /* a color role (e.g. "background") or a meta key (e.g. "display") */
    char value[OSR_THEME_VALUE_LEN];
} osr_theme_kv;

typedef struct {
    char name[OSR_THEME_NAME_LEN];
    osr_theme_kv colors[OSR_THEME_MAX_COLORS];
    unsigned long color_count;
    osr_theme_kv meta[OSR_THEME_MAX_META];
    unsigned long meta_count;
    char configs[OSR_THEME_CONFIGS_LEN]; /* space-joined `config:` dir list */
} osr_theme_palette;

/* osr_load_theme_palette -- parse theme_list_path into *out, with out->name
 * set to theme_name (the file itself never states its own directory name).
 * Returns 1 on success, 0 if the file can't be opened.
 */
int osr_load_theme_palette(const char *theme_list_path, const char *theme_name, osr_theme_palette *out);

/* osr_theme_color_hex -- the raw value of a `color: <role> ...` line
 * ("#2e3440", or any other string -- not every color role is hex, see
 * osr_theme_kv above), or NULL if the theme defines no such role.
 */
const char *osr_theme_color_hex(const osr_theme_palette *p, const char *role);

/* osr_theme_meta_get -- the value of a single-valued meta key (display,
 * description, polarity, session, ...), or NULL if unset.
 */
const char *osr_theme_meta_get(const osr_theme_palette *p, const char *key);

#endif /* OSR_THEME_LIST_H */
