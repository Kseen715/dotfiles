/* lib/install.c -- the C behind install.sh: everything the runner does
 * that is text rather than orchestration.
 *
 * install.sh cannot become a C program yet, and pretending otherwise would
 * be the wrong slice: its middle is a sequence of calls into shell libraries
 * (osr_detect, osr_resolve_user, osr_apply_theme, pkg_install) and its module
 * loop SOURCES each module, which is shell by definition. What it also does,
 * and what lives here now, is decide and print:
 *
 *   usage                     the help text
 *   list-rices|list-modules   the two directory listings (--list/--list-modules)
 *   parse-args <argv...>      the option loop, as shell assignments to eval
 *   manifest <rice.list>      the manifest parser: modules + require: lines
 *   count <list>              the step denominator (§3), i.e. how many modules
 *   report base               distro/pkg/kernel/user, from the exported facts
 *   report hw                 the cpu/ram/hwaccel lines, same source
 *   final <mode> <mods> <verb> <rice>   the closing [DONE] line
 *
 * The report and final lines are printed as lib/log.sh's info()/success()
 * would print them (common.h's log_line), not handed back to the shell,
 * so the palette and the `%-8s` tag stay in exactly one implementation.
 *
 * Byte-for-byte with the sh original, frozen at test/ref/install_sh_ref.sh
 * and diffed by test/unit/install_c_parity.sh. The one deliberate
 * difference is documented at cmd_parse_args.
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

#include "common.h"
#include "cmds.h"
#include "apply.h"
#include "config.h"
#include "module.h"
#include "preflight.h"
#include "reload.h"

#include <glob.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ---------------------------------------------------------------------
 * the two listings
 * ------------------------------------------------------------------ */

/* osr_root -- $OSR_ROOT, which install.sh resolves from its own $0 and
 * exports before anything else happens. */
static const char *osr_root(void) { return env_str("OSR_ROOT", "."); }

/* list_dir -- the shared shape of list_rices/list_modules:
 *
 *     for f in "$OSR_ROOT"/<pattern>; do
 *         [ -f <marker> ] || continue
 *         printf '  %s\n' <name>
 *     done
 *
 * GLOB_NOCHECK because POSIX sh has no nullglob: an empty rices/ leaves the
 * pattern itself in the loop variable, whose marker file then does not
 * exist, so nothing is printed either way.
 */
static void list_dir(const char *pattern, const char *marker, const char *strip_suffix) {
    Str path;
    Str out;
    glob_t g;
    size_t i;

    str_init(&path);
    str_addz(&path, osr_root());
    str_addc(&path, '/');
    str_addz(&path, pattern);
    if (glob(str_text(&path), GLOB_NOCHECK, NULL, &g) != 0) { str_free(&path); return; }
    str_free(&path);

    str_init(&out);
    for (i = 0; i < g.gl_pathc; i++) {
        Str name;
        if (marker != NULL) {
            Str probe;
            int ok;
            str_init(&probe);
            str_addz(&probe, g.gl_pathv[i]);
            str_addc(&probe, '/');
            str_addz(&probe, marker);
            ok = file_exists(str_text(&probe));
            str_free(&probe);
            if (!ok) continue;
        } else if (!file_exists(g.gl_pathv[i])) {
            continue;
        }
        str_init(&name);
        base_of(&name, g.gl_pathv[i]);
        if (strip_suffix != NULL) {
            size_t sl = strlen(strip_suffix);
            if (name.len >= sl && strcmp(name.p + name.len - sl, strip_suffix) == 0) {
                name.len -= sl;              /* ${_b%.sh} */
                name.p[name.len] = '\0';
            }
        }
        str_addz(&out, "  ");
        str_add(&out, str_text(&name), name.len);
        str_addc(&out, '\n');
        str_free(&name);
    }
    globfree(&g);
    out_flush(&out);
    str_free(&out);
}

/* collect_dir -- list_dir's names, into out instead of stdout. */
static void collect_dir(Str *out, const char *pattern, const char *strip_suffix) {
    Str path;
    glob_t g;
    size_t i;

    str_init(&path);
    str_addz(&path, osr_root());
    str_addc(&path, '/');
    str_addz(&path, pattern);
    if (glob(str_text(&path), GLOB_NOCHECK, NULL, &g) != 0) { str_free(&path); return; }
    str_free(&path);
    for (i = 0; i < g.gl_pathc; i++) {
        Str name;
        if (!file_exists(g.gl_pathv[i])) continue;
        str_init(&name);
        base_of(&name, g.gl_pathv[i]);
        if (strip_suffix != NULL) {
            size_t sl = strlen(strip_suffix);
            if (name.len >= sl && strcmp(name.p + name.len - sl, strip_suffix) == 0) {
                name.len -= sl;
                name.p[name.len] = '\0';
            }
        }
        str_add(out, str_text(&name), name.len);
        str_addc(out, '\n');
        str_free(&name);
    }
    globfree(&g);
}

static int by_name(const void *a, const void *b) {
    return strcoll(*(const char *const *)a, *(const char *const *)b);
}

/* cmd_list_modules -- both kinds in one alphabetical list: the shell scripts
 * under modules/ and the C ones the core implements (lib/modules.c). A rice
 * never says which kind it wants, so neither does the listing. */
static void cmd_list_modules(void) {
    Str all;
    Str out;
    char **names = NULL;
    size_t count = 0;
    size_t cap = 0;
    size_t pos = 0;
    Line line;
    size_t i;

    str_init(&all);
    collect_dir(&all, "modules/*.sh", ".sh");
    osr_module_names(&all);

    while (next_line(str_text(&all), all.len, &pos, &line)) {
        if (line.len == 0) continue;
        if (count == cap) {
            cap = cap ? cap * 2 : 64;
            names = (char **)realloc(names, cap * sizeof(char *));
            if (names == NULL) osr_die_oom();
        }
        names[count] = (char *)malloc(line.len + 1);
        if (names[count] == NULL) osr_die_oom();
        memcpy(names[count], line.start, line.len);
        names[count][line.len] = '\0';
        count++;
    }
    if (count > 1) qsort(names, count, sizeof(char *), by_name);

    str_init(&out);
    for (i = 0; i < count; i++) {
        str_addz(&out, "  ");
        str_addz(&out, names[i]);
        str_addc(&out, '\n');
        free(names[i]);
    }
    free(names);
    out_flush(&out);
    str_free(&out);
    str_free(&all);
}

/* ---------------------------------------------------------------------
 * usage
 * ------------------------------------------------------------------ */

static void usage_to(FILE *fp) {
    Str o;
    str_init(&o);
    str_addz(&o, "Usage:\n");
    str_addz(&o, "  install.sh [--user <name>] [--verbose] [--theme <name>] <rice>\n");
    str_addz(&o, "                                                    install a rice\n");
    str_addz(&o, "  install.sh --module [--theme <name>] <name>...    install module(s), no rice\n");
    str_addz(&o, "  install.sh --theme-only --theme <name>            apply a theme only (no\n");
    str_addz(&o, "                                                    packages, no sudo) - see osr\n");
    str_addz(&o, "  install.sh --list                                 list available rices\n");
    str_addz(&o, "  install.sh --list-themes                          list available themes\n");
    str_addz(&o, "  install.sh --list-modules                         list available modules\n");
    str_addz(&o, "\n");
    str_addz(&o, "  <rice>            name of a directory under os-rice/rices/\n");
    str_addz(&o, "  --module          treat positionals as module names, not a rice\n");
    str_addz(&o, "  --theme <name>    which theme supplies the 90-* appearance layers. In rice\n");
    str_addz(&o, "                    mode it overrides the manifest's own `theme:`; in --module\n");
    str_addz(&o, "                    mode it is the interactive picker's answer (default theme\n");
    str_addz(&o, "                    if no TTY)\n");
    str_addz(&o, "  --user <name>     account to install for (default: invoking user)\n");
    str_addz(&o, "  --verbose         stream command output instead of spinners\n");
    fwrite(str_text(&o), 1, o.len, fp);
    fflush(fp);
    str_free(&o);
}

/* cmd_usage -- the help on stdout: `--help` is an answer. The two paths that
 * end in "you did not say what to install" send it to stderr instead, which is
 * what install.sh's `usage >&2` did. */
static void cmd_usage(void) { usage_to(stdout); }

/* ---------------------------------------------------------------------
 * the option loop
 * ------------------------------------------------------------------ */

/* cmd_parse_args -- install.sh's `while [ $# -gt 0 ]` loop, printing the
 * variables it set for the shell to eval. Options that ended the run there
 * (--list, --help, an unknown flag) come back as OSR_ACTION, because those
 * paths call into shell libraries (list_themes) or must exit the shell.
 *
 * ONE DELIBERATE DIFFERENCE from the sh original: an option missing its
 * operand was `${2:?--user needs a name}`, whose diagnostic is the SHELL's
 * ("install.sh: 85: 2: --user needs a name", exit 2) and differs between
 * dash and bash anyway. It now comes back as OSR_ACTION=missing-arg and
 * install.sh reports it through error(), like every other bad-input case:
 * "[ERROR] --user needs a name", exit 1.
 */
/* Args -- what the option loop decided. `action` is set only for the paths that
 * end the run right there (a listing, the help, a bad option); everything else
 * leaves it empty and the run continues with the rest of the fields. */
typedef struct {
    const char *user;
    const char *theme;
    const char *module_mode;
    const char *theme_only;
    const char *no_reload;
    const char *action;
    const char *action_arg;
    int verbose;
    Str pos;
} Args;

static void parse_args(Args *a, int argc, char **argv) {
    const char *user = "";
    const char *theme = "";
    const char *module_mode = "";
    const char *theme_only = "";
    const char *no_reload = "";
    const char *action = "";
    const char *action_arg = "";
    int verbose = 0;
    int i;
    Str pos;

    str_init(&pos);
    for (i = 0; i < argc && action[0] == '\0'; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--user") == 0 || strcmp(a, "--theme") == 0) {
            int is_user = (strcmp(a, "--user") == 0);
            if (i + 1 >= argc || argv[i + 1][0] == '\0') {
                action = "missing-arg";
                action_arg = is_user ? "--user needs a name" : "--theme needs a theme name";
                break;
            }
            if (is_user) user = argv[i + 1];
            else theme = argv[i + 1];
            i++;
        } else if (strcmp(a, "--theme-only") == 0) {
            theme_only = "1";
        } else if (strcmp(a, "--no-reload") == 0) {
            no_reload = "1";
        } else if (strcmp(a, "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(a, "--module") == 0) {
            module_mode = "1";
        } else if (strcmp(a, "--list") == 0) {
            action = "list";
        } else if (strcmp(a, "--list-themes") == 0) {
            action = "list-themes";
        } else if (strcmp(a, "--list-modules") == 0) {
            action = "list-modules";
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            action = "usage";
        } else if (a[0] == '-') {
            action = "error";
            action_arg = a;
        } else {
            /* OSR_POS="$OSR_POS $1" -- the leading space is part of it, and
             * the field splitting of the result is what makes "only one
             * rice may be given" detectable later. */
            str_addc(&pos, ' ');
            str_addz(&pos, a);
        }
    }

    a->user = user;
    a->theme = theme;
    a->module_mode = module_mode;
    a->theme_only = theme_only;
    a->no_reload = no_reload;
    a->action = action;
    a->action_arg = action_arg;
    a->verbose = verbose;
    a->pos = pos;
}

static int cmd_parse_args(int argc, char **argv) {
    Args a;
    Str out;

    parse_args(&a, argc, argv);
    str_init(&out);
    sh_assign(&out, "OSR_ARG_USER", a.user);
    sh_assign(&out, "OSR_ARG_THEME", a.theme);
    sh_assign(&out, "OSR_MODULE_MODE", a.module_mode);
    sh_assign(&out, "OSR_THEME_ONLY", a.theme_only);
    sh_assign(&out, "OSR_NO_RELOAD", a.no_reload);
    sh_assign(&out, "OSR_POS", str_text(&a.pos));
    sh_assign(&out, "OSR_ACTION", a.action);
    sh_assign(&out, "OSR_ACTION_ARG", a.action_arg);
    if (a.verbose) str_addz(&out, "OSR_VERBOSE=1; export OSR_VERBOSE\n");
    out_flush(&out);
    str_free(&out);
    str_free(&a.pos);
    return 0;
}

/* ---------------------------------------------------------------------
 * the manifest
 * ------------------------------------------------------------------ */

/* cmd_manifest -- the rice.list reader:
 *
 *     _line=${_line%%#*}                        strip comments
 *     _line=$(printf %s "$_line" | sed <trim [[:space:]] both ends>)
 *     [ -n "$_line" ] || continue
 *     require:*  -> OSR_REQUIRES
 *     theme:* / themes:*  -> ignored here (lib/theme.sh reads them)
 *     *          -> OSR_MODULES
 *
 * The `|| [ -n "$_line" ]` on the read loop means a final line with no
 * newline still counts; so does it here.
 */
static int read_manifest(Str *modules, Str *requires_, const char *path) {
    FILE *fp;
    Str line;
    int c;
    int done = 0;

    fp = fopen(path, "rb");
    if (fp == NULL) return 0;

    str_init(&line);
    while (!done) {
        size_t start = 0;
        size_t end;
        const char *text;

        c = fgetc(fp);
        if (c == EOF) {
            done = 1;
            if (line.len == 0) break;
        } else if (c != '\n') {
            str_addc(&line, (char)c);
            continue;
        }

        if (line.len == 0) {
            /* a blank line: nothing to strip, nothing to dispatch */
            str_reset(&line);
            continue;
        }
        /* ${_line%%#*} */
        for (end = 0; end < line.len && line.p[end] != '#'; end++) {
            /* scan */
        }
        line.len = end;
        line.p[end] = '\0';
        /* trim */
        while (start < line.len && is_space(line.p[start])) start++;
        end = line.len;
        while (end > start && is_space(line.p[end - 1])) end--;
        line.p[end] = '\0';
        text = line.p + start;

        if (*text != '\0') {
            if (strncmp(text, "require:", 8) == 0) {
                str_addc(requires_, ' ');
                str_addz(requires_, text + 8);
            } else if (strncmp(text, "theme:", 6) == 0 || strncmp(text, "themes:", 7) == 0) {
                /* osr_rice_default_theme / osr_rice_themes read these */
            } else {
                str_addc(modules, ' ');
                str_addz(modules, text);
            }
        }
        str_reset(&line);
    }
    fclose(fp);
    str_free(&line);
    return 1;
}

static int cmd_manifest(const char *path) {
    Str modules, requires_, out;

    str_init(&modules);
    str_init(&requires_);
    if (!read_manifest(&modules, &requires_, path)) {
        str_free(&modules);
        str_free(&requires_);
        return 1;
    }
    str_init(&out);
    sh_assign(&out, "OSR_MODULES", str_text(&modules));
    sh_assign(&out, "OSR_REQUIRES", str_text(&requires_));
    out_flush(&out);
    str_free(&out);
    str_free(&modules);
    str_free(&requires_);
    return 0;
}

/* cmd_count -- `for _m in $LIST; do n=$((n+1)); done`: the module count that
 * becomes OSR_STEP_TOTAL, i.e. field splitting on IFS whitespace. */
static long count_words(const char *list) {
    const char *p = list;
    long n = 0;
    while (*p != '\0') {
        while (*p != '\0' && is_space(*p)) p++;
        if (*p == '\0') break;
        n++;
        while (*p != '\0' && !is_space(*p)) p++;
    }
    return n;
}

static int cmd_count(const char *list) {
    printf("%ld\n", count_words(list));
    return 0;
}

/* ---------------------------------------------------------------------
 * the report lines
 * ------------------------------------------------------------------ */

static void info_line(Str *out, const char *msg) {
    log_line(out, "OSR_CYAN", "[INFO]", NULL, msg);
}

/* opt -- `${VAR:+ prefix$VAR suffix}`: append only when the variable is set
 * and non-empty, which is how every one of these lines drops the facets
 * that were not detected (§7). */
static void opt(Str *s, const char *lead, const char *var, const char *tail) {
    const char *v = env_str(var, NULL);
    if (v == NULL) return;
    str_addz(s, lead);
    str_addz(s, v);
    str_addz(s, tail);
}

static void report_base(Str *out) {
    Str l;
    struct utsname u;

    str_init(&l);
    str_addz(&l, "distro=");
    str_addz(&l, env_str("OSR_DISTRO", ""));
    opt(&l, " version_id=", "OSR_VERSION_ID", "");
    opt(&l, " codename=", "OSR_CODENAME", "");
    opt(&l, " version=\"", "OSR_VERSION", "\"");
    info_line(out, str_text(&l));

    str_reset(&l);
    opt(&l, "id_like=\"", "OSR_ID_LIKE", "\" ");
    str_addz(&l, "pkg=");
    str_addz(&l, env_str("OSR_PKG", ""));
    str_addz(&l, " init=");
    str_addz(&l, env_str("OSR_INIT", ""));
    info_line(out, str_text(&l));

    str_reset(&l);
    str_addz(&l, "kernel=");
    if (uname(&u) == 0) str_addz(&l, u.release); /* sh: $(uname -r) */
    info_line(out, str_text(&l));

    str_reset(&l);
    str_addz(&l, "user=");
    str_addz(&l, env_str("OSR_USER", ""));
    str_addz(&l, " home=");
    str_addz(&l, env_str("OSR_HOME", ""));
    info_line(out, str_text(&l));

    str_free(&l);
}

static void report_hw(Str *out) {
    Str hw;
    Str ram;
    Str accel;
    long cores = env_long("OSR_CPU_CORES", 0);
    long threads = env_long("OSR_CPU_THREADS", 0);
    long sticks = env_long("OSR_RAM_STICKS", 0);
    long channels = env_long("OSR_RAM_CHANNELS", 0);

    /* cpu: the model is optional, the arch never is; threads only when SMT
     * actually doubles them up ("cores=4 threads=4" is noise). */
    str_init(&hw);
    opt(&hw, "cpu=", "OSR_CPU_MODEL", " ");
    str_addz(&hw, "arch=");
    str_addz(&hw, env_str("OSR_CPU_ARCH", ""));
    if (cores > 0) { str_addz(&hw, " cores="); str_addl(&hw, cores); }
    if (threads > cores) { str_addz(&hw, " threads="); str_addl(&hw, threads); }
    if (strcmp(env_str("OSR_VIRT", ""), "none") != 0) {
        str_addz(&hw, " virt=");
        str_addz(&hw, env_str("OSR_VIRT", ""));
    }
    info_line(out, str_text(&hw));
    str_free(&hw);

    str_init(&ram);
    opt(&ram, "ram=", "OSR_RAM_TOTAL", "");
    if (env_str("OSR_RAM_TYPE", NULL) != NULL) {
        if (ram.len > 0) str_addc(&ram, ' ');
        str_addz(&ram, env_str("OSR_RAM_TYPE", ""));
    }
    if (env_str("OSR_RAM_SPEED", NULL) != NULL) {
        if (ram.len > 0) str_addc(&ram, ' ');
        str_addz(&ram, env_str("OSR_RAM_SPEED", ""));
    }
    if (sticks > 0) { str_addz(&ram, " sticks="); str_addl(&ram, sticks); }
    if (channels > 0) { str_addz(&ram, " channels="); str_addl(&ram, channels); }
    if (ram.len > 0) info_line(out, str_text(&ram));
    str_free(&ram);

    str_init(&accel);
    if (env_str("OSR_GPU_MODEL", NULL) != NULL) {
        str_addz(&accel, "gpu=");
        str_addz(&accel, env_str("OSR_GPU_MODEL", ""));
    } else if (env_str("OSR_GPU_VENDOR", NULL) != NULL) {
        str_addz(&accel, "gpu=");
        str_addz(&accel, env_str("OSR_GPU_VENDOR", ""));
    }
    if (env_str("OSR_NPU_VENDOR", NULL) != NULL) {
        if (accel.len > 0) str_addc(&accel, ' ');
        str_addz(&accel, "npu=");
        str_addz(&accel, env_str("OSR_NPU_VENDOR", ""));
    }
    if (accel.len > 0) {
        Str line;
        str_init(&line);
        str_addz(&line, "hwaccel: ");
        str_add(&line, str_text(&accel), accel.len);
        info_line(out, str_text(&line));
        str_free(&line);
    }
    str_free(&accel);
}

static int cmd_report(const char *which) {
    Str out;
    str_init(&out);
    if (strcmp(which, "base") == 0) report_base(&out);
    else if (strcmp(which, "hw") == 0) report_hw(&out);
    else { str_free(&out); return 2; }
    out_flush(&out);
    str_free(&out);
    return 0;
}

/* cmd_final -- the closing success() line: which one depends on how the run
 * was started, and `switch` is install with a different sentence (§6). */
static void final_line(const char *module_mode, const char *modules,
                       const char *mode, const char *rice) {
    Str out;
    Str l;

    str_init(&l);
    if (module_mode[0] != '\0') {
        str_addz(&l, "module(s) installed:");
        str_addz(&l, modules);
    } else if (strcmp(mode, "switch") == 0) {
        str_addz(&l, "switched to rice '");
        str_addz(&l, rice);
        str_addz(&l, "' (packages accreted, theme layers replaced)");
    } else {
        str_addz(&l, "rice '");
        str_addz(&l, rice);
        str_addz(&l, "' installed");
    }

    str_init(&out);
    log_line(&out, "OSR_GREEN", "[DONE]", NULL, str_text(&l));
    out_flush(&out);
    str_free(&out);
    str_free(&l);
}

static int cmd_final(const char *module_mode, const char *modules,
                     const char *mode, const char *rice) {
    final_line(module_mode, modules, mode, rice);
    return 0;
}

/* ---------------------------------------------------------------------
 * the runner
 *
 * install.sh's body. It stayed shell for one reason -- it SOURCED each module,
 * and only a shell can source a shell script into itself. No module is a shell
 * script any more (§11a), so the orchestration comes here and install.sh
 * becomes what `osr` already is: a line that execs the core.
 *
 * The two tiers still coexist by contract: a `.sh` module that appears again is
 * run the only way one can be, by handing it to a shell with the libs sourced
 * around it. That is the same command install.sh ran, spelled once here.
 * ------------------------------------------------------------------ */

/* The libs a shell module is sourced into, in install.sh's order. */
static const char *const SH_MODULE_LIBS =
    ". \"$OSR_LIB/ui.sh\"; . \"$OSR_LIB/log.sh\"; "
    "for l in detect user net pkg git service config migrate theme apply reload "
    "fonts gnome build preflight; do [ -f \"$OSR_LIB/$l.sh\" ] && . \"$OSR_LIB/$l.sh\"; done; "
    ". \"$1\"";

/* list_themes -- the one listing that was never in the core: it is a query into
 * the theme manifests, not a directory scan (a theme is a dir with a
 * theme.list, and the line carries its description). */
static void list_themes(void) {
    Str all;
    size_t pos = 0;
    Line line;

    str_init(&all);
    osr_theme_list(&all);
    while (next_line(str_text(&all), all.len, &pos, &line)) {
        Str name, desc, out;
        size_t pad;
        if (line.len == 0) continue;
        str_init(&name); str_init(&desc); str_init(&out);
        str_add(&name, line.start, line.len);
        osr_theme_meta(&desc, str_text(&name), "description");
        str_addz(&out, "  ");
        str_addz(&out, str_text(&name));
        for (pad = name.len; pad < 12; pad++) str_addc(&out, ' ');  /* %-12s */
        str_addc(&out, ' ');
        str_addz(&out, str_text(&desc));
        str_addc(&out, '\n');
        out_flush(&out);
        str_free(&name); str_free(&desc); str_free(&out);
    }
    str_free(&all);
}

/* sudo_warmup -- warm the sudo credential for the whole run so escalating steps
 * do not each prompt (§7). Best-effort and interactive-only: root-for-root and
 * non-root user-space rices (§8) need no sudo, and CI/containers run as root --
 * so a missing TTY is never fatal here; steps escalate lazily via as_root. */
static void sudo_warmup(void) {
    char *argv[3];
    pid_t keeper;

    if (geteuid() == 0 || !osr_have_cmd("sudo") || !isatty(0)) return;
    argv[0] = (char *)"sudo"; argv[1] = (char *)"-v"; argv[2] = NULL;
    if (osr_run_quiet(argv) != 0) return;

    /* The keep-alive: re-stamp the ticket every minute, and exit with the run.
     * Detached so it cannot hold the terminal, and it dies when its parent
     * does -- kill(0) is how the shell version noticed. */
    fflush(stdout);
    keeper = fork();
    if (keeper != 0) return;
    {
        pid_t parent = getppid();
        char *keep[4];
        keep[0] = (char *)"sudo"; keep[1] = (char *)"-n"; keep[2] = (char *)"true";
        keep[3] = NULL;
        for (;;) {
            (void)osr_run_quiet(keep);
            sleep(60);
            if (kill(parent, 0) != 0) _exit(0);
        }
    }
}

/* ram_retry -- the RAM line needs DMI (type/speed/slots) and DMI needs root, so
 * the probe is re-run after the sudo warm-up. Only worth installing dmidecode
 * where SMBIOS exists at all: the entry point is present-but-unreadable for
 * non-root, and most ARM SoCs have no DMI tables. */
static void ram_retry(void) {
    static const char *const dmi[] = { "dmidecode", NULL };

    if (env_long("OSR_RAM_STICKS", 0) != 0) return;
    if (!osr_have_cmd("dmidecode")
        && file_exists("/sys/firmware/dmi/tables/smbios_entry_point")) {
        /* Forked: a failed install (no perms, no net) must not abort the run --
         * the RAM line just degrades to the size from /proc/meminfo. */
        pid_t pid;
        fflush(stdout);
        pid = fork();
        if (pid == 0) _exit(osr_pkg_install(dmi) ? 0 : 1);
        if (pid > 0) { int st; (void)waitpid(pid, &st, 0); }
    }
    osr_detect_export("ram");
}

/* run_sh_module -- the coexistence path: hand a `.sh` module to a shell with
 * the libs sourced around it, which is the only way one can run. */
static int run_sh_module(const char *path) {
    char *argv[6];
    argv[0] = (char *)"sh";
    argv[1] = (char *)"-c";
    argv[2] = (char *)SH_MODULE_LIBS;
    argv[3] = (char *)"sh";      /* $0; the path below is the script's $1 */
    argv[4] = (char *)path;
    argv[5] = NULL;
    return osr_run(argv) == 0;
}

/* run_one -- one manifest entry, whichever tier owns it. The core wins where
 * both exist, and a rice.list never says which kind it asked for.
 *
 * A failing module ends the run, which is what a failing run_step inside a .sh
 * module did: a half-applied mutation is not something to limp on from. It ends
 * it SILENTLY -- the module has already said why, and install.sh added nothing
 * of its own here either (`set -e` simply stopped). */
static void run_one(const char *mod, long *n) {
    Str msg, path;

    *n += 1;
    {
        char num[32];
        sprintf(num, "%ld", *n);
        setenv("OSR_STEP_N", num, 1);
    }
    str_init(&msg);
    str_addz(&msg, "module: ");
    str_addz(&msg, mod);
    osr_log_step(str_text(&msg));
    str_free(&msg);

    str_init(&path);
    if (osr_module_has(mod)) {
        if (!osr_module_run(mod, 0)) exit(1);
        str_free(&path);
        return;
    }
    str_addz(&path, osr_root());
    str_addz(&path, "/modules/");
    str_addz(&path, mod);
    str_addz(&path, ".sh");
    if (!file_exists(str_text(&path)))
        osr_die("module not found: %s (%s)", mod, str_text(&path));
    if (!run_sh_module(str_text(&path))) exit(1);
    str_free(&path);
}

/* each_word -- `for x in $list` over an IFS-split string. */
static void each_word(const char *list, void (*fn)(const char *, void *), void *ctx) {
    const char *p = list;
    while (*p != '\0') {
        const char *start;
        Str w;
        while (*p != '\0' && is_space(*p)) p++;
        start = p;
        while (*p != '\0' && !is_space(*p)) p++;
        if (p == start) break;
        str_init(&w);
        str_add(&w, start, (size_t)(p - start));
        fn(str_text(&w), ctx);
        str_free(&w);
    }
}

static void check_module_name(const char *m, void *ctx) {
    Str path;
    (void)ctx;
    if (osr_module_has(m)) return;
    str_init(&path);
    str_addz(&path, osr_root());
    str_addz(&path, "/modules/");
    str_addz(&path, m);
    str_addz(&path, ".sh");
    if (!file_exists(str_text(&path)))
        osr_die("module not found: %s (try --list-modules)", m);
    str_free(&path);
}

static void any_themable(const char *m, void *ctx) {
    if (osr_module_themable(m)) *(int *)ctx = 1;
}

static void run_word(const char *m, void *ctx) { run_one(m, (long *)ctx); }

static void one_rice(const char *p, void *ctx) {
    Str *rice = (Str *)ctx;
    if (rice->len > 0)
        osr_die("only one rice may be given (got '%s' and '%s')", str_text(rice), p);
    str_addz(rice, p);
}

static int cmd_run(int argc, char **argv) {
    Args a;
    Str modules, requires_, rice;
    long total, n = 0;
    char num[32];

    parse_args(&a, argc, argv);
    if (a.verbose) setenv("OSR_VERBOSE", "1", 1);

    if (strcmp(a.action, "usage") == 0)        { cmd_usage(); return 0; }
    if (strcmp(a.action, "list") == 0)         { printf("Available rices:\n");
                                                 list_dir("rices/*/", "rice.list", NULL);
                                                 return 0; }
    if (strcmp(a.action, "list-themes") == 0)  { printf("Available themes:\n");
                                                 list_themes(); return 0; }
    if (strcmp(a.action, "list-modules") == 0) { printf("Available modules:\n");
                                                 cmd_list_modules(); return 0; }
    if (strcmp(a.action, "error") == 0)        osr_die("unknown option: %s", a.action_arg);
    if (strcmp(a.action, "missing-arg") == 0)  osr_die("%s", a.action_arg);

    /* --- detection + identity ------------------------------------------- */
    osr_detect_export("all");
    osr_resolve_user(a.user);

    /* --- theme-only: the hotkey path (§6a) -------------------------------
     * Everything below this block - the hardware report, the sudo warm-up, the
     * DMI probe that may INSTALL dmidecode - exists to make package decisions.
     * A theme apply makes none, and it runs from a key press with no terminal
     * attached, so it must not touch a package manager, prompt for a password,
     * or take a second. It returns here rather than threading an `if` through
     * the rest of the function. */
    if (a.theme_only[0] != '\0') {
        (void)osr_apply_theme(a.theme);
        if (a.no_reload[0] == '\0') osr_reload_all();
        {
            Str msg;
            str_init(&msg);
            str_addz(&msg, "theme '");
            str_addz(&msg, env_str("OSR_THEME", ""));
            str_addz(&msg, "' applied");
            osr_success_line(str_text(&msg));
            str_free(&msg);
        }
        str_free(&a.pos);
        return 0;
    }

    /* The detected facts, one line each, only the facets that were detected
     * (§7). VERSION_ID is absent on rolling releases and drops out of the line
     * there. */
    (void)cmd_report("base");
    sudo_warmup();
    ram_retry();
    (void)cmd_report("hw");

    /* --- resolve what to run: a rice manifest, or explicit --module names -- */
    str_init(&modules); str_init(&requires_); str_init(&rice);
    if (a.module_mode[0] != '\0') {
        int themable = 0;
        Str want;

        str_addz(&modules, str_text(&a.pos));
        if (count_words(str_text(&modules)) == 0) {
            usage_to(stderr);
            osr_die("no module specified");
        }
        each_word(str_text(&modules), check_module_name, NULL);

        /* A standalone module still gets theme-owned 90-* layers, but only
         * where a theme has anywhere to land: `osr module benchmark` installs
         * stress-ng and reads no theme file at any point, so putting the picker
         * in front of it asked a question whose answer was then discarded. */
        each_word(str_text(&modules), any_themable, &themable);

        /* An explicit --theme is honoured whatever the module set: naming a
         * theme is not a question. Neither is a theme this box has already been
         * painted with -- that is the answer the picker would be asking for, so
         * a machine with a theme applied installs a module in that theme
         * instead of stopping to ask again. A recorded theme that no longer
         * exists in themes/ falls back to asking rather than aborting the run,
         * which is why this is a directory test and not osr_theme_exists (that
         * one is fatal on a miss, by design, for a name the user typed). */
        str_init(&want);
        str_addz(&want, a.theme);
        if (want.len == 0) {
            Str dir;
            osr_state_get(&want, "theme");
            str_init(&dir);
            str_addz(&dir, osr_root());
            str_addz(&dir, "/themes/");
            str_addz(&dir, str_text(&want));
            if (want.len == 0 || !dir_exists(str_text(&dir))) str_reset(&want);
            str_free(&dir);
        }
        if (a.theme[0] != '\0' || themable) osr_resolve_theme(str_text(&want));
        else                                 osr_unset_theme();
        str_free(&want);
    } else {
        Str list, want;

        each_word(str_text(&a.pos), one_rice, &rice);
        if (rice.len == 0) { usage_to(stderr); osr_die("no rice specified"); }
        str_init(&list);
        str_addz(&list, osr_root());
        str_addz(&list, "/rices/");
        str_addz(&list, str_text(&rice));
        setenv("OSR_RICE_DIR", str_text(&list), 1);
        str_addz(&list, "/rice.list");
        if (!file_exists(str_text(&list)))
            osr_die("rice not found: %s (try --list)", str_text(&rice));
        setenv("OSR_RICE", str_text(&rice), 1);
        if (!read_manifest(&modules, &requires_, str_text(&list)))
            osr_die("rice not found: %s (try --list)", str_text(&rice));
        str_free(&list);

        /* The theme that owns this run's 90-* layers: --theme wins, else the
         * manifest's `theme:`, else the default. Overriding it is supported on
         * purpose - a rice is a package set, and any theme paints any of them. */
        str_init(&want);
        str_addz(&want, a.theme);
        if (want.len == 0) osr_rice_default_theme(&want, str_text(&rice));
        if (want.len == 0) str_addz(&want, env_str("OSR_DEFAULT_THEME", "xin"));
        osr_resolve_theme(str_text(&want));
        str_free(&want);

        /* Preconditions (§10 Tier 1): fail clean before any mutation if the
         * host cannot run this rice. Runs on switch too - you cannot switch
         * into a rice the hardware cannot support. */
        if (requires_.len > 0) {
            /* Split in place into a NULL-terminated vector, sized from the
             * manifest itself so a rice with any number of predicates is
             * checked in full -- a truncated list would pass a host that the
             * dropped predicate rules out. */
            const char **want_list;
            size_t count = 0;
            Str copy;
            char *p;

            str_init(&copy);
            str_addz(&copy, str_text(&requires_));
            want_list = (const char **)malloc((count_words(str_text(&requires_)) + 1)
                                              * sizeof *want_list);
            if (want_list == NULL) osr_die_oom();
            p = copy.p;
            while (*p != '\0') {
                while (*p != '\0' && is_space(*p)) *p++ = '\0';
                if (*p == '\0') break;
                want_list[count++] = p;
                while (*p != '\0' && !is_space(*p)) p++;
            }
            want_list[count] = NULL;
            if (count > 0) osr_preflight(want_list);
            free((void *)want_list);
            str_free(&copy);
        }
    }

    total = count_words(str_text(&modules));
    sprintf(num, "%ld", total);
    setenv("OSR_STEP_TOTAL", num, 1);
    setenv("OSR_STEP_N", "0", 1);

    each_word(str_text(&modules), run_word, &n);

    /* --- theme-owned whole-dir configs + wallpaper (rice mode only) --------
     * The `config:` dirs come from the THEME's manifest, not the rice's: they
     * are appearance (GTK colors, xsettingsd, fontconfig) and must travel with
     * the theme onto whichever rice it is applied to. */
    if (a.module_mode[0] == '\0') {
        (void)osr_apply_theme_configs();
        (void)osr_apply_wallpaper();
        /* Record what is now applied. `rice` is what makes a later `osr theme`
         * cheap AND correct: it narrows the layer set to this manifest's
         * modules, so a theme switch never writes configs for programs this
         * rice never installed. */
        (void)osr_state_set("rice", str_text(&rice));
        (void)osr_state_set("theme", env_str("OSR_THEME", ""));
        {
            char stamp[32];
            sprintf(stamp, "%ld", (long)time(NULL));
            (void)osr_state_set("applied", stamp);
        }
    }

    final_line(a.module_mode, str_text(&modules),
               env_str("OSR_MODE", "install"), str_text(&rice));

    str_free(&modules); str_free(&requires_); str_free(&rice); str_free(&a.pos);
    return 0;
}

static int usage_err(void) {
    fputs("usage: osr install <subcommand> [args]\n\n", stderr);
    fputs("  usage                        the install.sh help text\n", stderr);
    fputs("  list-rices | list-modules    the two directory listings\n", stderr);
    fputs("  parse-args <argv...>         option loop -> shell assignments\n", stderr);
    fputs("  manifest <rice.list>         manifest parser -> shell assignments\n", stderr);
    fputs("  count <list>                 how many modules (the step total)\n", stderr);
    fputs("  report base | hw             the detected-facts lines\n", stderr);
    fputs("  final <mode> <mods> <verb> <rice>   the closing [DONE] line\n", stderr);
    fputs("  run <argv...>                the whole installer (install.sh)\n", stderr);
    return 2;
}

int osr_install_main(int argc, char **argv) {
    if (argc < 2) return usage_err();

    if (strcmp(argv[1], "usage") == 0) {
        cmd_usage();
        return 0;
    }
    if (strcmp(argv[1], "list-rices") == 0) {
        list_dir("rices/*/", "rice.list", NULL);
        return 0;
    }
    if (strcmp(argv[1], "list-modules") == 0) {
        cmd_list_modules();
        return 0;
    }
    if (strcmp(argv[1], "parse-args") == 0) return cmd_parse_args(argc - 2, argv + 2);
    if (strcmp(argv[1], "manifest") == 0 && argc == 3) return cmd_manifest(argv[2]);
    if (strcmp(argv[1], "count") == 0 && argc == 3) return cmd_count(argv[2]);
    if (strcmp(argv[1], "report") == 0 && argc == 3) return cmd_report(argv[2]);
    if (strcmp(argv[1], "final") == 0 && argc == 6) {
        return cmd_final(argv[2], argv[3], argv[4], argv[5]);
    }
    if (strcmp(argv[1], "run") == 0) return cmd_run(argc - 2, argv + 2);
    return usage_err();
}
