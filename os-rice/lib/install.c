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

#include <glob.h>
#include <sys/utsname.h>

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

static void cmd_usage(void) {
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
    out_flush(&o);
    str_free(&o);
}

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
static int cmd_parse_args(int argc, char **argv) {
    Str out;
    Str pos;
    const char *user = "";
    const char *theme = "";
    const char *module_mode = "";
    const char *theme_only = "";
    const char *no_reload = "";
    const char *action = "";
    const char *action_arg = "";
    int verbose = 0;
    int i;

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

    str_init(&out);
    sh_assign(&out, "OSR_ARG_USER", user);
    sh_assign(&out, "OSR_ARG_THEME", theme);
    sh_assign(&out, "OSR_MODULE_MODE", module_mode);
    sh_assign(&out, "OSR_THEME_ONLY", theme_only);
    sh_assign(&out, "OSR_NO_RELOAD", no_reload);
    sh_assign(&out, "OSR_POS", str_text(&pos));
    sh_assign(&out, "OSR_ACTION", action);
    sh_assign(&out, "OSR_ACTION_ARG", action_arg);
    if (verbose) str_addz(&out, "OSR_VERBOSE=1; export OSR_VERBOSE\n");
    out_flush(&out);
    str_free(&out);
    str_free(&pos);
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
static int cmd_manifest(const char *path) {
    FILE *fp;
    Str modules;
    Str requires_;
    Str line;
    Str out;
    int c;
    int done = 0;

    fp = fopen(path, "rb");
    if (fp == NULL) return 1;

    str_init(&modules);
    str_init(&requires_);
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
                str_addc(&requires_, ' ');
                str_addz(&requires_, text + 8);
            } else if (strncmp(text, "theme:", 6) == 0 || strncmp(text, "themes:", 7) == 0) {
                /* osr_rice_default_theme / osr_rice_themes read these */
            } else {
                str_addc(&modules, ' ');
                str_addz(&modules, text);
            }
        }
        str_reset(&line);
    }
    fclose(fp);

    str_init(&out);
    sh_assign(&out, "OSR_MODULES", str_text(&modules));
    sh_assign(&out, "OSR_REQUIRES", str_text(&requires_));
    out_flush(&out);
    str_free(&out);
    str_free(&line);
    str_free(&modules);
    str_free(&requires_);
    return 0;
}

/* cmd_count -- `for _m in $LIST; do n=$((n+1)); done`: the module count that
 * becomes OSR_STEP_TOTAL, i.e. field splitting on IFS whitespace. */
static int cmd_count(const char *list) {
    const char *p = list;
    long n = 0;
    while (*p != '\0') {
        while (*p != '\0' && is_space(*p)) p++;
        if (*p == '\0') break;
        n++;
        while (*p != '\0' && !is_space(*p)) p++;
    }
    printf("%ld\n", n);
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
static int cmd_final(const char *module_mode, const char *modules,
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
    return usage_err();
}
