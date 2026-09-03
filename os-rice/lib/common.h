/* lib/common.h -- the shared kit behind the harness core, on either system.
 *
 * There is one binary per host (build/osr, build/osr.exe), built by nob.c from
 * osr.c plus one translation unit per subsystem, and this is the kit those
 * units are written against. It is deliberately the SAME header on both:
 * lib/common.c carries a POSIX body and a Win32 body for the handful of
 * questions only a kernel can answer, and everything else in it is one piece
 * of C89 that both cores compile. That is what lets osr.c, lib/modules.c and
 * every module be one file rather than two. These are the pieces more than
 * one unit needs:
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
 *   osr_*f       the same five log lines taking a printf format, which is how
 *                a module and the package layer say things
 *
 * These helpers reproduce a few of sh's behaviours on purpose -- word
 * splitting, `printf %b`, the way a missing value reads as empty -- because
 * the formats and manifests they parse were written against sh's rules.
 *
 * C89 + POSIX, and C89 + Win32.
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

/* osr_color_mode -- how, if at all, this descriptor can be colored.
 *
 * Three answers rather than the shell's two, because "can it take color" and
 * "can it take an ESCAPE SEQUENCE" are not the same question off POSIX. ANSI
 * is `[ -t 1 ] && [ -z "$NO_COLOR" ]` and everything the palette assumes;
 * CLASSIC is a Windows console old enough to interpret no escape at all,
 * where color exists but only through the console API (lib/common.c's
 * osr_paint_tagged); NONE is a pipe, a file, or NO_COLOR.
 *
 * A POSIX build never returns CLASSIC. Callers that only want "should there
 * be color" can compare against OSR_COLOR_NONE and ignore the distinction. */
#define OSR_COLOR_NONE    0
#define OSR_COLOR_ANSI    1
#define OSR_COLOR_CLASSIC 2
int osr_color_mode(int fd);

/* osr_paint_tagged -- log_line's shape printed through whatever this terminal
 * actually supports: the ANSI composition on POSIX and on a modern console,
 * the console API on an old one. prefix may be NULL. Used by the log lines
 * themselves; a caller composing its own text wants log_line. */
void osr_paint_tagged(FILE *stream, const char *color_env, const char *tag,
                      const char *prefix, const char *msg);

/* osr_term_cols_raw -- the terminal's own width, 0 when nothing can say. The
 * kernel/console half of term_cols; the $COLUMNS override and ui.sh's floors
 * are applied by term_cols around it. */
long osr_term_cols_raw(void);

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

/* OSR_TAG_WIDTH -- every line this installer prints reserves this many
 * columns for its bracketed tag, so the messages all start in the same
 * column. It is lib/log.sh's `%-8s`, now shared with the run_step tags
 * ([ok]/[!!]/[--]) and the spinner frame, which used to print a single
 * space after the tag and so began their text three columns to the left
 * of every [INFO] line around them. */
#define OSR_TAG_WIDTH 8

/* tag_pad -- append the spaces that carry a tag of `tag_len` visible
 * columns out to OSR_TAG_WIDTH (at least one, so a tag at or past the
 * width still separates from its message). Call it AFTER the $OSR_NC
 * reset: the padding is not part of the colored run. */
void tag_pad(Str *out, size_t tag_len);

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

/* osr_debug_line -- the same, gated on $OSR_DEBUG. */
void osr_debug_line(const char *msg);
void osr_success_line(const char *msg);
void osr_error_line(const char *msg);

/* OSR_LOG_MSG_MAX -- how much of one formatted log line is kept. Log text is
 * a message, not a document: everything printed through these is a status
 * line, a path, or a package name. */
#define OSR_LOG_MSG_MAX 2048

/* The same five lines taking a printf format. This is the form a module and
 * the package layer use (lib/module.h documents them as the module's saying
 * verbs); the plain-string forms above are what the commands themselves use
 * when they already hold a finished string.
 *
 * osr_die prints and exits 1 -- lib/log.sh's error(), the one fatal path.
 * The formatted text is passed on as data: a log message is never itself
 * used as a format string. */
void osr_infof(const char *fmt, ...);
void osr_debugf(const char *fmt, ...);
void osr_warnf(const char *fmt, ...);
void osr_successf(const char *fmt, ...);
void osr_die(const char *fmt, ...);

/* osr_stepf -- osr_infof carrying install.sh's "[03/12] " step counter.
 * total == 0 prints no counter at all, which is what a standalone
 * `osr module <name>` gets. */
void osr_stepf(unsigned long n, unsigned long total, const char *fmt, ...);

/* ---------------------------------------------------------------------
 * small file helpers every command needs
 * ------------------------------------------------------------------ */

/* slurp -- a whole file as a malloc'd NUL-terminated buffer (*len excludes
 * the NUL), NULL when it cannot be read. */
char *slurp(const char *path, size_t *len);
int file_exists(const char *path);
int dir_exists(const char *path);
/* osr_path_taken -- `[ -e path ]`: is anything at all at this path? Not the
 * same question as file_exists -- a dangling symlink is not openable but IS
 * already spoken for, and a seed-once layer must not overwrite one. */
int osr_path_taken(const char *path);
/* osr_absolute_dir -- an existing directory's canonical absolute path, the
 * `cd -- "$dir" && pwd` that turns a relative pick into something storable.
 * 0 when it cannot be resolved, and the caller keeps what it had. */
int osr_absolute_dir(const char *dir, char *out, unsigned long out_sz);
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

/* OSR_PATH_MAX -- how long a path this tree is willing to compose. Not the
 * platform's own limit: it is the size of the fixed buffers a module or a
 * builder declares, chosen to hold any real config path with room to spare,
 * and the helpers below refuse rather than truncate when a path exceeds it. */
#define OSR_PATH_MAX 1024

/* Bounded path composition. Each returns 0 and leaves the buffer EMPTY when
 * the result would not fit, because a truncated path is a command that acts
 * on the wrong file -- never a shortened path that still looks usable.
 *
 * osr_path_join writes '/' as the separator on both systems: every Windows
 * API this tree calls accepts it, and one separator keeps a path comparable
 * no matter which half of the tree composed it. */
int osr_copy_bounded(char *dst, unsigned long dst_sz, const char *src);
int osr_append_bounded(char *dst, unsigned long dst_sz, const char *suffix);
int osr_path_join(char *out, unsigned long out_sz, const char *a, const char *b);
/* osr_dirname -- the directory holding path, "." when it has none. Both
 * separators are honoured: argv[0] on Windows carries whichever was typed. */
void osr_dirname(const char *path, char *out, unsigned long out_sz);
/* osr_basename -- the last component, as a pointer into path itself. */
const char *osr_basename(const char *path);

/* osr_home -- the home directory of the account being riced: $OSR_HOME first
 * (a sudo'd or elevated run is not running as that account), else $HOME or
 * %USERPROFILE%. osr_expand_home rewrites a leading "~" with it, and leaves a
 * path that has none alone. */
const char *osr_home(void);
void osr_expand_home(const char *path, char *out, unsigned long out_sz);

/* osr_mkdir_parents -- `mkdir -p`, without the fork. */
int osr_mkdir_parents(const char *dir);
/* osr_copy_file -- copy src over dst, creating dst's parent tree first.
 * Overwrites unconditionally: this is a dotfiles rice, and the repository is
 * the source of truth. A caller that must not clobber an edit wants
 * osr_seed_file (lib/module.h) instead. */
int osr_copy_file(const char *src, const char *dst);
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

/* ---------------------------------------------------------------------
 * the process, the filesystem, the terminal
 * ------------------------------------------------------------------ */

/* osr_setenv / osr_unsetenv -- this process's environment, which is what
 * every child it spawns inherits. That inheritance is the point: the runner
 * detects the facts once and every module sees them, which is what `export`
 * bought the shell tier. There is no setenv() in the Microsoft C runtime, so
 * this is not a macro over one. */
void osr_setenv(const char *name, const char *value);
void osr_unsetenv(const char *name);

long osr_pid(void);

/* osr_tmpdir -- where a scratch file goes: $TMPDIR, then %TEMP%/%TMP% where
 * those exist. Honoured on both systems, so a test that points TMPDIR at a
 * sandbox is obeyed wherever it runs. */
const char *osr_tmpdir(void);

/* osr_list_dir -- the listing behind every "what is available" answer: rices,
 * themes, modules.
 *
 * With `marker`, the SUBDIRECTORIES of dir containing a file of that name --
 * which is what makes "a theme is a directory carrying a theme.list" a
 * definition rather than a convention. With `strip_suffix`, the FILES whose
 * name ends with it, that suffix removed. With NEITHER, every entry, which is
 * what a `glob` of the directory answered. A name beginning with a dot is never
 * listed, in any of the three, because glob's `*` did not match one either.
 *
 * Names are sorted and appended one per line, for next_line to walk. Sorted
 * because a person reads this and a listing whose order changes between two
 * machines is one they cannot diff -- the same reason the glob() it replaces
 * sorted, and that glob is also why this exists: the Windows C runtime has
 * none. */
void osr_list_dir(Str *out, const char *dir, const char *marker,
                  const char *strip_suffix);

/* osr_interactive -- is there a person at the other end, in both directions?
 * A picker has to print a prompt AND read an answer. */
int osr_interactive(void);

/* osr_tty_open -- the controlling terminal, past any redirection, or NULL.
 * A picker must not read from stdin or print to stdout: either may be a pipe
 * carrying the value being asked for. Returns the read half and, through
 * out_stream (which may be NULL), the write half -- two streams because
 * Windows names them separately (CONIN$/CONOUT$) where POSIX has the one
 * read-write /dev/tty. Closing them is the caller's. */
FILE *osr_tty_open(FILE **out_stream);

#endif /* OSR_COMMON_H */
