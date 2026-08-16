/* lib/manifest.h -- rice.list parser, C port of the inline `while read` loop
 * in install.sh (there is no separate lib/manifest.sh; that loop IS the
 * reference implementation this mirrors -- see install.sh's rice-mode
 * branch). Same three directive lines: `theme:`, `themes:`, `require:`;
 * everything else is a module name. `#` starts a comment anywhere on the
 * line, same as install.sh's `${_line%%#*}`.
 *
 * C89.
 */
#ifndef OSR_MANIFEST_H
#define OSR_MANIFEST_H

#define OSR_MANIFEST_MAX_NAME    128
#define OSR_MANIFEST_MAX_MODULES 256
#define OSR_MANIFEST_MAX_LIST    2048  /* space-joined `themes:`/`require:` */

typedef struct {
    char theme[OSR_MANIFEST_MAX_NAME];
    char themes[OSR_MANIFEST_MAX_LIST];
    char requires_list[OSR_MANIFEST_MAX_LIST];
    char modules[OSR_MANIFEST_MAX_MODULES][OSR_MANIFEST_MAX_NAME];
    unsigned long module_count;
} osr_manifest;

/* osr_parse_rice_list -- fills *out from the rice.list/theme.list-shaped
 * file at path. Returns 1 on success, 0 if the file can't be opened (*out
 * is zeroed either way).
 */
int osr_parse_rice_list(const char *path, osr_manifest *out);

#endif /* OSR_MANIFEST_H */
