/* lib/common.h -- the shared kit behind the POSIX harness core.
 *
 * The POSIX side is one binary (build/osr, built by nob.c from osr.c plus the
 * lib/osr_*.c translation units, exactly the way the Windows core links
 * install.c and its lib units). These are the pieces more than one of them
 * needs:
 *
 *   Str          a grow-on-append byte buffer, so a command can compose its
 *                whole output and write it once
 *   expand_b     printf's `%b` conversion -- the sh originals print colors
 *                and status lines with it, so a port that skips it differs
 *                on any string carrying a backslash
 *   env_*        getenv with sh's "unset and empty are the same" reading,
 *                which is how `${VAR:-}` and `[ -z "$VAR" ]` behave
 *   log_line     lib/log.sh's one output shape:
 *                printf '%b%-8s%b%s\n' <color> <tag> <reset> <message>
 *   palette      the `[ -t 1 ] && [ -z "$NO_COLOR" ]` decision, plus the six
 *                escape strings it hands out
 *   sh_quote     values handed back to a shell to eval
 *
 * The contract for the whole rewrite is byte-for-byte identical output: each
 * sh original is frozen under test/ref/ and diffed against its C replacement
 * by a unit test, so these helpers reproduce sh's quirks on purpose.
 *
 * C89 + POSIX.
 */
#ifndef OSR_COMMON_H
#define OSR_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------
 * Str -- a grow-on-append byte buffer
 * ------------------------------------------------------------------ */
typedef struct {
    char *p;
    size_t len;
    size_t cap;
} Str;

void osr_die_oom(void);
void str_init(Str *s);
void str_add(Str *s, const char *b, size_t n);
void str_addz(Str *s, const char *z);
void str_addc(Str *s, char c);
void str_addl(Str *s, long n);
/* str_reset -- empty the buffer, keeping the allocation. */
void str_reset(Str *s);
void str_free(Str *s);
/* str_text -- the bytes, never NULL (an untouched Str reads as ""). */
const char *str_text(const Str *s);
/* str_trim_trailing -- drop trailing bytes of the given class, which is what
 * a `$(...)` did to newlines. */
void str_trim_trailing(Str *s, char c);
/* str_add_squeezed -- append `len` bytes with every run of whitespace collapsed
 * to one space and the ends trimmed.
 *
 * This is a display rule, not a parsing one, and it exists for one field: the
 * CPU model. Intel pads its brand string inside the name -- "Intel(R) Core(TM)
 * i7-8550U  CPU @ 1.80GHz" reaches userspace with the runs intact, and lscpu
 * and /proc/cpuinfo both hand it over verbatim -- so trimming only the ends
 * leaves a ragged gap in the middle of every line the name appears on. */
void str_add_squeezed(Str *s, const char *p, size_t len);

/* out_flush -- write everything composed so far in one go, then flush. */
void out_flush(Str *s);
/* err_flush -- the same, to stderr. */
void err_flush(Str *s);

/* sh_quote -- append 'value' with sh single-quote escaping, so any byte
 * survives an eval; sh_assign adds NAME= in front and a newline after. */
void sh_quote(Str *out, const char *value);
void sh_assign(Str *out, const char *name, const char *value);

/* ---------------------------------------------------------------------
 * printf %b
 * ------------------------------------------------------------------ */

/* expand_b -- append s to out with printf's `%b` conversion applied:
 * backslash escapes are interpreted, `\c` stops output (and swallows the
 * rest of that printf, its trailing newline included). Returns 1 when a
 * `\c` was hit, so callers can drop what the shell would have dropped.
 */
int expand_b(Str *out, const char *s);

/* ---------------------------------------------------------------------
 * environment, terminal, palette
 * ------------------------------------------------------------------ */

/* env_str -- getenv, with sh's "unset and empty are the same thing"
 * reading (`${VAR:-}` / `[ -z "$VAR" ]`). */
const char *env_str(const char *name, const char *dflt);
int env_is_set(const char *name);
/* env_long -- numeric env var; anything sh's `[ x -gt 0 ]` could not use
 * (empty, non-numeric, trailing junk) reads as dflt. */
long env_long(const char *name, long dflt);

/* color -- a palette variable (OSR_RED, OSR_NC, ...) as lib/ui.sh exports
 * it: the LITERAL text '\033[0;31m', still to be run through expand_b. */
const char *color(const char *name);

int fd_is_open(int fd);
/* query_fd -- which descriptor answers "is stdout a terminal?". Normally
 * fd 1; a helper called inside a `$(...)` has a capture pipe there, so the
 * shims dup the shell's real stdout to fd 3 and we prefer that when open. */
int query_fd(void);

/* OSR_PALETTE_VARS -- the six names, in the order lib/ui.sh assigned them. */
#define OSR_PALETTE_COUNT 6
extern const char *const osr_palette_names[OSR_PALETTE_COUNT];
/* osr_palette_values -- the six LITERAL escape strings when color applies,
 * six empty strings when it does not: ui.sh's
 * `[ -t 1 ] && [ -z "${NO_COLOR:-}" ]`, asked of fd. */
const char *const *osr_palette_values(int fd);

/* term_cols -- ui.sh's `tput cols` -> $COLUMNS -> 80 chain, without the
 * fork; widths under 20 fall back to 80, as in sh. */
long term_cols(void);

/* ---------------------------------------------------------------------
 * lib/log.sh's line shape
 * ------------------------------------------------------------------ */

/* log_line -- printf '%b%-8s%b%s\n' "$color" "$tag" "$OSR_NC" "$msg",
 * appended to out. color_env names the palette variable ("OSR_CYAN"), tag
 * is the bracketed label ("[INFO]"), prefix is install.sh's "[03/12] "
 * step counter or NULL. The message is a plain `%s`: no expansion, ever -
 * log text is data.
 */
void log_line(Str *out, const char *color_env, const char *tag,
              const char *prefix, const char *msg);
/* osr_info / osr_warn / osr_error_line -- one whole log line, printed now.
 * osr_error_line does NOT exit: only the shell can end a run (lib/log.sh's
 * error() keeps the `exit`). */
void osr_info(const char *msg);
void osr_warn(const char *msg);
void osr_success_line(const char *msg);
void osr_error_line(const char *msg);

/* ---------------------------------------------------------------------
 * small file helpers every command needs
 * ------------------------------------------------------------------ */

/* slurp -- a whole file as a malloc'd NUL-terminated buffer (*len excludes
 * the NUL), NULL when it cannot be read. */
char *slurp(const char *path, size_t *len);
int file_exists(const char *path);
int dir_exists(const char *path);
/* next_line -- iterate a buffer line by line. A final line without a
 * newline is still a line, to sed, to grep, and here. */
typedef struct {
    const char *start;
    size_t len;      /* without the newline */
    int had_newline;
} Line;
int next_line(const char *buf, size_t buf_len, size_t *pos, Line *out);
/* is_space -- POSIX [[:space:]] in the C locale. */
int is_space(char c);
/* base_of -- `basename "$p"`: the last component, trailing slashes ignored. */
void base_of(Str *out, const char *path);
/* osr_files_equal -- `cmp -s a b`. */
int osr_files_equal(const char *a, const char *b);

/* ---------------------------------------------------------------------
 * running other programs
 * ------------------------------------------------------------------ */

/* osr_path_lookup -- `command -v <name>`: is there an executable of that name
 * on $PATH? The resolved path is appended to out when out is not NULL. */
int osr_path_lookup(const char *name, Str *out);
/* osr_run_quiet -- run argv with stdout and stderr on /dev/null, returning its
 * exit status: every `>/dev/null 2>&1` probe the sh libs made. */
int osr_run_quiet(char *const argv[]);

#endif /* OSR_COMMON_H */
