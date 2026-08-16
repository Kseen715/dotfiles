/* lib/wallpaper.h -- theme wallpapers, C port of the wallpaper half of
 * lib/config.sh (osr_is_image/osr_theme_wallpapers/osr_theme_wallpaper/
 * osr_install_wallpaper_file/osr_wallpaper_set_live/osr_choose_wallpaper/
 * osr_wallpaper_library) plus install.sh's apply_wallpaper. windows-rice
 * has no wallpaper support to port from (grep confirms -- only a mention
 * in a comment that it doesn't exist there yet), so this is a fresh port
 * straight from the sh original, using Win32's SystemParametersInfo to
 * paint the desktop where lib/config.sh shells out to swww/hyprctl/feh.
 *
 * NOT ported: the flat `~/.config/osr/wallpaper` file lib/config.sh also
 * writes for non-shell consumers (a lock screen, a bar, Proteus) -- none
 * of those exist on this tier yet, so there's nothing to read it. Only
 * the state.h "wallpaper" key is written. A known, documented gap.
 *
 * C89.
 */
#ifndef OSR_WALLPAPER_H
#define OSR_WALLPAPER_H

#define OSR_WALLPAPER_MAX_LIBRARY 200
#define OSR_WALLPAPER_PATH_LEN    600

/* osr_is_image -- 1 for a file with a known image extension
 * (.jpg/.jpeg/.png/.webp/.bmp/.gif, any case), else 0.
 */
int osr_is_image(const char *path);

/* osr_theme_wallpapers -- every image under <theme_dir>/wallpapers/, in
 * lexical (filename) order -- the first is the theme's default. Writes up
 * to out_max paths into out, returns how many.
 */
unsigned long osr_theme_wallpapers(const char *theme_dir, char out[][OSR_WALLPAPER_PATH_LEN], unsigned long out_max);

/* osr_theme_wallpaper -- the wallpaper to use for `theme` right now: the
 * user's recorded per-theme choice if it still exists on disk, else the
 * theme's own first wallpaper. out is "" if neither exists.
 */
void osr_theme_wallpaper(const char *theme_dir, const char *theme, char *out, unsigned long out_sz);

/* osr_install_wallpaper_file -- copy src into
 * %USERPROFILE%\Pictures\Wallpapers (skipped if an identical-content copy
 * is already there). Writes the installed path into out. Returns 1 on
 * success.
 */
int osr_install_wallpaper_file(const char *src, char *out, unsigned long out_sz);

/* osr_wallpaper_set_live -- paint path on the desktop right now. Best
 * effort: returns 1 on success, 0 if the OS refused (headless/RDP
 * session, or a format SystemParametersInfo won't take on this Windows
 * version -- reliably JPG/PNG needs Vista+; XP wants BMP). A 0 here is
 * not meant to be fatal to a caller, same as the sh original.
 */
int osr_wallpaper_set_live(const char *path);

/* osr_choose_wallpaper -- make path the wallpaper of theme: record the
 * per-theme choice, install a copy into the library, paint it live.
 * Writes the installed path into out. Returns 1 on success (path must
 * already be an image; use osr_is_image to check first).
 */
int osr_choose_wallpaper(const char *theme, const char *path, char *out, unsigned long out_sz);

/* osr_apply_theme_wallpaper -- resolve + install + paint + record
 * (the generic "wallpaper" state key, not the per-theme one -- see this
 * file's own header comment) for a fresh theme apply. Returns 1 whether
 * or not the theme ships a wallpaper at all (nothing to do is success).
 */
int osr_apply_theme_wallpaper(const char *theme_dir, const char *theme);

/* osr_wallpaper_library -- every image the user can pick between: the
 * theme's own set first, then whatever is already in
 * %USERPROFILE%\Pictures\Wallpapers, deduplicated by filename. Writes up
 * to out_max paths into out, returns how many.
 */
unsigned long osr_wallpaper_library(const char *theme_dir, char out[][OSR_WALLPAPER_PATH_LEN], unsigned long out_max);

#endif /* OSR_WALLPAPER_H */
