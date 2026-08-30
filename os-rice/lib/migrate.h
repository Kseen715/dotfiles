/* lib/migrate.h -- C port of lib/migrate.sh: patch the layers install_layer can
 * never reach.
 *
 * 00-env.zsh and 99-local.zsh are seeded once and are then user territory (§5).
 * That is what keeps a rice switch non-destructive -- but it also means a fix
 * shipped in the repo never reaches a box installed before it. These helpers
 * close that gap without breaking the ownership rule: they only ever append
 * text that cannot destroy anything, or replace a region that matches, byte for
 * byte, what os-rice itself shipped. A region the user edited fails the exact
 * match and the caller reports it instead of guessing.
 *
 * Where the shell version took the names of two functions whose OUTPUT was the
 * old and new region, the C version takes that text directly: a C caller
 * already has the strings, and the fork-per-region the shell needed bought
 * nothing.
 *
 * C89 + POSIX.
 */
#ifndef OSR_MIGRATE_H
#define OSR_MIGRATE_H

/* osr_migrate_append -- append text to file when detect_ere matches no line in
 * it. Purely additive, so it is safe on a user-owned file whatever they put
 * there. A blank line is written before the text, as the shell version's
 * `{ printf '\n'; cat; }` did. Always returns 1. */
int osr_migrate_append(const char *file, const char *detect_ere,
                       const char *label, const char *text);

/* osr_migrate_replace -- replace the first exact occurrence of old with new.
 * new may be empty, which deletes the region. Returns 0 when the region is
 * absent or no longer byte-identical -- the signal for the caller to warn
 * rather than force anything.
 *
 * Idempotent by construction: after a successful run the old text is gone, so
 * a re-run finds no match and returns 0 without touching the file.
 *
 * The comparison is literal, never a regex: these regions contain $, [, * and
 * backslashes, and an exact match is the entire safety property. */
int osr_migrate_replace(const char *file, const char *label, const char *old,
                        const char *new_text);

/* osr_migrate_stale -- warn that a legacy pattern is still present but no
 * longer matches anything os-rice shipped, so it must be resolved by hand.
 * Never fatal: a box that cannot be auto-patched still installs.
 *
 * detect_ere is matched against CODE lines only -- a line whose first non-blank
 * character is not '#'. The replacement text these migrations write explains
 * what it replaced, so a naive match would hit the fix itself and warn forever
 * after. Skipping comments is what makes a successful migration quiet on the
 * next run. Always returns 1. */
int osr_migrate_stale(const char *file, const char *detect_ere,
                      const char *what);

#endif /* OSR_MIGRATE_H */
