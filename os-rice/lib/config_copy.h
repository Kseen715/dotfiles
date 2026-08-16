/* lib/config_copy.h -- config file installation, C port of
 * windows-rice/src/config.ps1's Copy-ConfigEntry (the file half; directory
 * copy is not ported -- none of the four modules this session ports need
 * it, see modules.c).
 *
 * Default is to overwrite without asking, same choice config.ps1 already
 * made and documents in its own header: "this is a dotfiles rice, the repo
 * is the source of truth." No `-Ask` confirm-before-overwrite here.
 *
 * C89.
 */
#ifndef OSR_CONFIG_COPY_H
#define OSR_CONFIG_COPY_H

/* osr_expand_home -- replace a leading "~" (bare, or followed by / or \)
 * with the user's home directory (%USERPROFILE% on Windows, $HOME
 * elsewhere), into out (bounded). A path with no leading ~ passes through
 * unchanged. Mirrors the ~\... paths windows-rice's own modules write
 * literally (PowerShell expands those itself; this is that expansion,
 * done explicitly, for C).
 */
void osr_expand_home(const char *path, char *out, unsigned long out_sz);

/* osr_copy_file -- copy src to dst, creating dst's parent directory tree
 * first if needed. Overwrites dst unconditionally. Returns 1 on success.
 */
int osr_copy_file(const char *src, const char *dst);

#endif /* OSR_CONFIG_COPY_H */
