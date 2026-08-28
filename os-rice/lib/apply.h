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

/* osr_apply_verbs -- every function name defined by the libs whose functions
 * all mutate the system (pkg build net git service fonts), one per line. Read
 * out of the sources rather than a hand-kept list so it cannot drift: a new
 * provider added to lib/build.sh is inert in a theme apply the day it is
 * written, with no edit to any list. */
void osr_apply_verbs(Str *out);

/* osr_apply_query_ok -- the read-only exceptions to that set: queries with no
 * side effect that modules branch on. Removing a query from this list is safe
 * (the branch just takes its unknown path); adding a mutating verb to it is
 * not. */
int osr_apply_is_query(const char *name);

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

#endif /* OSR_APPLY_H */
