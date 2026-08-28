/* lib/config.h -- the C port of lib/config.sh: layered config by ownership.
 *
 * §5. Every config is split along ownership layers with distinct lifecycles,
 * and os-rice writes only what it owns:
 *
 *   00-env   user/machine   seeded once if absent, then kept
 *   10/20-*  dotfiles       overwrite on update, rice-independent
 *   90-*     rice           swapped on rice switch
 *   99-local machine        seeded empty once, never touched
 *
 * Where a single target file is unavoidable (.zshrc, .xprofile) os-rice owns
 * only a marked block; where the app has no include mechanism at all
 * (starship.toml, settings.json) the installed file is COMPOSED from a
 * dotfiles base plus a rice fragment, and is generated output either way --
 * edit the base or the fragment, never the result.
 *
 * The theme-template half of lib/config.sh (render_theme_template,
 * osr_theme_source, install_theme_layer) already lives in lib/render.c and
 * lib/module.c; this unit is the layering, composing and app-version-adapting
 * half. The wallpaper half is still lib/config.sh's.
 *
 * Return 1 for success. The paths lib/config.sh spelled `error ...` are
 * osr_die here, as there.
 *
 * C89 + POSIX.
 */
#ifndef OSR_CONFIG_H
#define OSR_CONFIG_H

#include "common.h"

/* --- seeded layers (00-env, 99-local) ------------------------------------- */
/* osr_seed_once -- copy src to dst only when dst is absent. After seeding, dst
 * is the machine's and os-rice never rewrites it. */
int osr_seed_once(const char *src, const char *dst);
/* osr_seed_empty -- create dst empty when absent (99-local). */
int osr_seed_empty(const char *dst);

/* --- owned blocks inside a file os-rice does not own ---------------------- */
/* osr_compose_block -- what the file should look like with the os-rice block
 * for `name` holding `body`: everything outside the markers verbatim, the
 * region between them replaced, appended when there is no region yet. This is
 * the whole of ensure_block except the writing, and `osr user compose-block`
 * is this function with the body on stdin. */
void osr_compose_block(Str *out, const char *path, const char *name, const char *body);
/* osr_ensure_block -- the same, written back as the riced user. */
int osr_ensure_block(const char *path, const char *name, const char *body);

/* The three blocks os-rice owns, each a loader that sources a drop-in dir in
 * lexical order (§5), except the zshenv one -- see lib/config.sh for why that
 * has to be .zshenv and not an rc.d layer. */
int osr_install_zsh_loader(const char *rc_dir, const char *zshrc);
int osr_install_zsh_zshenv(const char *zshenv);
int osr_install_xprofile_loader(const char *dir, const char *xprofile);

/* --- composed configs (no include mechanism) ------------------------------ */
/* osr_compose_json_config -- the dotfiles base with the rice's keys merged
 * over it. Falls back to the base alone when there is no fragment or no
 * python3: a missing merge tool is a cosmetic loss, not a failed rice (§9). */
int osr_compose_json_config(const char *base, const char *frag, const char *dst);
/* osr_compose_starship_config -- the base body (its default palette table,
 * which must stay last in the file, stripped) followed by the rice's. */
int osr_compose_starship_config(const char *base, const char *frag, const char *dst);

/* --- configs adapted to the installed app (§9: degrade, never break) ------ */
/* osr_install_foot_palette -- foot 1.26 renamed the palette sections; the
 * palettes are written with the new names and downgraded for an older foot,
 * which would otherwise refuse to start. */
int osr_install_foot_palette(const char *src, const char *dst);
/* osr_install_alacritty_config -- Alacritty 0.14 moved `import` into
 * [general]; the section is dropped for 0.13, which would ignore the whole
 * file and lose the palette with it. */
int osr_install_alacritty_config(const char *src, const char *dst);

/* --- whole directories ---------------------------------------------------- */
/* osr_apply_config -- themes/<theme>/config/<name> into ~/.config/<name>, for
 * the theme.list `config:` directive. */
int osr_apply_config(const char *name);

/* --- Mozilla profiles ------------------------------------------------------ */
/* osr_mozilla_profiles -- every profile directory under root (~/.mozilla/
 * firefox), one per line: profiles.ini when there is one, else the *.default*
 * glob so a profile made before that file still gets the layer. */
void osr_mozilla_profiles(Str *out, const char *root);
/* osr_install_mozilla_layer -- user.js and/or chrome/userChrome.css into every
 * profile. Either path may be NULL or "". A profile-less app warns rather than
 * guessing a name the app would ignore. */
int osr_install_mozilla_layer(const char *root, const char *user_js, const char *user_chrome);


/* --- wallpaper: one resolution, one installed copy (§6) --------------------
 *
 * Four consumers want the same answer (hyprpaper's `preload =`, hyprland's
 * `env = WALLPAPER_PATH`, gtklock's background, the recorded state), so it is
 * resolved once here, installed once into a user-owned directory, and every
 * consumer is handed the same absolute path.
 *
 * The Windows tier has its own wallpaper unit (lib/wallpaper.c, painting
 * through SystemParametersInfo). These names are the POSIX ones and the two
 * are never linked into the same binary -- see nob.c's two source lists.
 */

/* osr_is_image -- a file whose extension says it is an image. Filtered by
 * extension on purpose: a theme's `wallpapers/README.txt` must never resolve
 * to a wallpaper, because painting a text file is worse than painting none. */
int osr_is_image(const char *path);

/* osr_theme_wallpapers -- every image a theme ships, one per line, in glob
 * order. The first is its default; the rest are what the picker scrolls. */
void osr_theme_wallpapers(Str *out, const char *theme_dir);

/* osr_theme_wallpaper -- the wallpaper the current theme should use, "" when
 * it ships none. A recorded per-theme choice wins over the theme default,
 * which is what makes a picked wallpaper survive switching away and back. */
void osr_theme_wallpaper(Str *out);

/* osr_install_wallpaper -- install the current theme's wallpaper and yield
 * where it landed ("" when there is none). */
void osr_install_wallpaper(Str *out);

/* osr_install_wallpaper_file -- copy one image into ~/Pictures/Wallpapers and
 * yield the installed path. That copy, not the path inside the checkout, is
 * what the configs point at, so the wallpaper survives moving the repo. */
void osr_install_wallpaper_file(Str *out, const char *src);

/* osr_install_wallpaper_layer -- install a config layer carrying the
 * {{WALLPAPER_PATH}} placeholder, substituting the installed wallpaper. A
 * theme with no wallpaper substitutes empty: the config still lands. */
int osr_install_wallpaper_layer(const char *src, const char *dst);

/* osr_wallpaper_set_live -- hand the image to whichever setter this session
 * has (swww, hyprpaper, feh). Best-effort: a headless box has nothing to
 * paint and that is not a failure (§9). */
void osr_wallpaper_set_live(const char *img);

/* osr_wallpaper_record -- the one place the applied wallpaper is written
 * down: ~/.config/osr/wallpaper for non-shell consumers, and the state file
 * keyed by theme so a per-theme choice survives a switch. */
void osr_wallpaper_record(const char *img);

/* osr_apply_wallpaper -- install + record + paint, the whole theme-apply
 * step. Degrades to record-only when headless. */
int osr_apply_wallpaper(void);

/* osr_wallpaper_library -- every image the user can choose between: the
 * theme's own first, then ~/Pictures/Wallpapers (which accretes across
 * themes), deduplicated by basename. */
void osr_wallpaper_library(Str *out);

/* osr_choose_wallpaper -- make `path` the current theme's wallpaper: record
 * the choice, install a copy into the library, paint it. Yields the installed
 * path; the setter's own logging goes to stderr, because this function's
 * stdout IS its answer. */
void osr_choose_wallpaper(Str *out, const char *path);

#endif /* OSR_CONFIG_H */
