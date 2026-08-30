/* lib/log.c -- the C behind lib/log.sh: info / debug / warn / success /
 * error, the five lines this installer says anything with.
 *
 * All five are the same shape, lib/log.sh's one printf:
 *
 *     printf '%b%-8s%b%s\n' "$OSR_CYAN" "[INFO]" "$OSR_NC" "$*"
 *
 * so the tag is padded to 8 columns, the palette goes through `%b` (the
 * variables lib/ui.sh exports hold the LITERAL text '\033[0;36m'), and the
 * message goes through `%s` -- never expanded, because log text is data.
 * With no palette in the environment every color reads as "" and the output
 * degrades to plain text exactly as the sh version did when ui.sh had not
 * been sourced. See lib/common.h's log_line.
 *
 * Which stream, and the one behavior that is not just printing:
 *
 *   info     stdout   cyan
 *   debug    stderr   dim     -- printed only when $OSR_DEBUG is non-empty
 *   warn     stderr   yellow
 *   success  stdout   green
 *   error    stderr   red     -- exit status 1; the SHELL still owns the
 *                                actual `exit 1`, see lib/log.sh's shim,
 *                                because only it can end the run
 *
 * `step` is info with install.sh's "[03/12] " counter in front of the
 * message, which the sh side spelled `info "$(step_prefix)module: $_mod"`.
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

#include "common.h"
#include "cmds.h"

/* emit -- one log line to stream. Composed in full, then written once. */
static void emit(FILE *stream, const char *color_env, const char *tag,
                 const char *prefix, const char *msg) {
    Str out;
    str_init(&out);
    log_line(&out, color_env, tag, prefix, msg);
    if (out.len > 0) fwrite(str_text(&out), 1, out.len, stream);
    fflush(stream);
    str_free(&out);
}

/* step_prefix -- install.sh's "[03/12] ", from the exported counters. An
 * unknown total (0) means no prefix at all, same as ui.sh's step_prefix.
 * Returns NULL when there is nothing to prefix. */
static const char *step_prefix(char *buf, size_t buf_sz) {
    long total = env_long("OSR_STEP_TOTAL", 0);
    long n = env_long("OSR_STEP_N", 0);
    if (total <= 0) return NULL;
    sprintf(buf, "[%02ld/%02ld] ", n, total);
    (void)buf_sz;
    return buf;
}

/* osr_step_prefix -- the same "[03/12] ", for a caller composing its own line
 * (the theme apply's per-layer debug). "" when no total is known. */
const char *osr_step_prefix(char *buf, size_t buf_sz) {
    const char *p = step_prefix(buf, buf_sz);
    return p != NULL ? p : "";
}

/* osr_log_step -- an [INFO] line carrying install.sh's "[03/12] " counter, for
 * a caller inside this process. The counters come from the environment, so a
 * forked module's own lines line up with the runner's. */
void osr_log_step(const char *msg) {
    char prefix[32];
    emit(stdout, "OSR_CYAN", "[INFO]", step_prefix(prefix, sizeof(prefix)), msg);
}

static int usage(void) {
    fputs("usage: osr log <level> <message>\n\n", stderr);
    fputs("  info | success        stdout\n", stderr);
    fputs("  warn | error          stderr (error exits 1)\n", stderr);
    fputs("  debug                 stderr, only when $OSR_DEBUG is set\n", stderr);
    fputs("  step                  info, with the \"[03/12] \" step counter\n", stderr);
    return 2;
}

int osr_log_main(int argc, char **argv) {
    const char *level;
    const char *msg;

    if (argc != 3) return usage();
    level = argv[1];
    msg = argv[2];

    if (strcmp(level, "info") == 0) {
        emit(stdout, "OSR_CYAN", "[INFO]", NULL, msg);
        return 0;
    }
    if (strcmp(level, "step") == 0) {
        osr_log_step(msg);
        return 0;
    }
    if (strcmp(level, "debug") == 0) {
        /* off unless OSR_DEBUG is set: a theme apply skips dozens of steps
         * by design, and printing each one would bury the handful of lines
         * that say what actually changed. */
        if (env_is_set("OSR_DEBUG")) emit(stderr, "OSR_DIM", "[DEBUG]", NULL, msg);
        return 0;
    }
    if (strcmp(level, "warn") == 0) {
        emit(stderr, "OSR_YELLOW", "[WARN]", NULL, msg);
        return 0;
    }
    if (strcmp(level, "success") == 0) {
        emit(stdout, "OSR_GREEN", "[DONE]", NULL, msg);
        return 0;
    }
    if (strcmp(level, "error") == 0) {
        emit(stderr, "OSR_RED", "[ERROR]", NULL, msg);
        return 1;
    }
    return usage();
}
