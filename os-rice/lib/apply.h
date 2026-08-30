/* lib/apply.h -- the portable half of lib/apply.sh.
 *
 * `osr theme <name>` re-runs the SAME modules a rice install runs -- never a
 * second copy of the mapping from a theme's config/ to the installed path,
 * which is the thing that would rot -- with every mutating verb neutralized
 * first. What survives is the file copying, which is what a theme is.
 *
 * Two of apply.sh's three pieces are pure file inspection and live here. The
 * third, osr_apply_stub_mutators, does NOT and cannot: it redefines shell
 * functions with `eval` so that the shell modules sourced afterwards call the
 * no-op instead of the real verb. That mechanism has no C equivalent while the
 * modules it protects are still `.sh`, so lib/apply.sh keeps it, and this unit
 * supplies the two lists it and osr_apply_theme are built out of. The C module
 * tier does not need the stubbing at all -- lib/install.c drives its own
 * theme-only pass by calling only the config verbs (see modules.h).
 *
 * C89 + POSIX.
 */
#ifndef OSR_APPLY_H
#define OSR_APPLY_H

/* osr_apply_verbs -- every verb a theme apply neutralizes, one per line.
 *
 * Derived from the sources rather than kept by hand, and derived from the code
 * that DOES the neutralizing: a function is in this list exactly when it asks
 * osr_theme_only() before acting. So the list cannot drift from the behaviour
 * -- a new mutating verb that remembers the check appears here the day it is
 * written, and one that forgets it is visibly absent.
 *
 * (The shell tier had no such check, so this was a scan for every function
 * defined by a lib that was mutating wholesale, plus a hand-kept list of
 * read-only exceptions. Per-function is both narrower and self-checking.) */
void osr_apply_verbs(Str *out);

/* osr_theme_modules -- the modules that carry a theme layer, one per line, in
 * manifest order when the installed rice is known.
 *
 * A module is theme-carrying when it names $OSR_THEME_DIR or goes through one
 * of the template helpers (install_theme_layer / osr_theme_source, which
 * resolve OSR_THEME_DIR themselves); that is the same search a person would
 * run, and it cannot go stale. Narrowing by the recorded rice matters: without
 * it a theme apply would write ~/.config/polybar on a Hyprland box, creating
 * configs for programs that are not installed. With no rice recorded (first
 * run, or a hand-built system) it overshoots rather than under-paints. */
void osr_theme_modules(Str *out, const char *rice);

/* osr_apply_theme -- the whole theme-only apply, for a caller that has already
 * resolved the target user (OSR_USER/OSR_HOME).
 *
 * Resolve the theme, neutralize every mutating verb for the rest of this
 * process (osr_set_theme_only), run the theme-carrying modules of the installed
 * rice with their output on the run log, then the theme's whole-dir configs and
 * the wallpaper, and record what was applied. A layer that fails is warned
 * about and skipped: a broken module must not leave the desktop half-painted.
 *
 * It lives here rather than in the runner so it can be driven against a
 * throwaway HOME -- the runner resolves OSR_HOME from passwd, so a test that
 * drove it through the runner would write to the real home of whoever runs the
 * suite. */
int osr_apply_theme(const char *name);

#endif /* OSR_APPLY_H */
