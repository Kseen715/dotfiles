/* lib/apply.c -- the portable half of lib/apply.sh. See lib/apply.h.
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "module.h"

#include "apply.h"
#include "cmds.h"
#include "config.h"

/* The markers that make a module theme-carrying. */
static const char *const THEME_MARKERS[] = {
    "OSR_THEME_DIR", "install_theme_layer", "osr_theme_source", NULL
};

/* def_name -- the function a C definition line declares.
 *
 * The shape every definition in lib/ has: it starts in column 0, and the line
 * ends in `{`. The name is the identifier immediately before the `(`, which
 * skips past the return type and any `static`. A prototype ends in `;` and a
 * call is indented, so neither is mistaken for a definition. */
static int def_name(Str *out, const Line *line) {
    const char *p = line->start;
    const char *end = line->start + line->len;
    const char *open;
    const char *name_end;
    const char *name;

    if (p >= end) return 0;
    if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || *p == '_'))
        return 0;                                   /* indented: not a definition */
    while (end > p && is_space(end[-1])) end--;
    if (end == p) return 0;
    /* `{` ends a definition; `,` or `)` ends the FIRST line of one whose
     * parameter list is split across lines, which several here are. A `;`
     * ends a prototype and is the one shape to refuse. */
    if (end[-1] != '{' && end[-1] != ',' && end[-1] != ')') return 0;

    open = NULL;
    for (name_end = p; name_end < end; name_end++)
        if (*name_end == '(') { open = name_end; break; }
    if (open == NULL) return 0;

    name_end = open;
    while (name_end > p && is_space(name_end[-1])) name_end--;
    name = name_end;
    while (name > p && ((name[-1] >= 'A' && name[-1] <= 'Z') ||
                        (name[-1] >= 'a' && name[-1] <= 'z') ||
                        (name[-1] >= '0' && name[-1] <= '9') ||
                        name[-1] == '_')) name--;
    if (name == name_end) return 0;
    str_add(out, name, (size_t)(name_end - name));
    return 1;
}

/* line_has_call -- is `needle` CALLED on this line? The lines are not
 * NUL-terminated, so strstr is not available -- and a match immediately after
 * a double quote is the needle inside a string literal, which is how this
 * very function would otherwise report itself. */
static int line_has_call(const Line *line, const char *needle) {
    size_t n = strlen(needle);
    size_t i;
    if (n > line->len) return 0;
    for (i = 0; i + n <= line->len; i++) {
        if (memcmp(line->start + i, needle, n) != 0) continue;
        if (i > 0 && line->start[i - 1] == '"') continue;
        return 1;
    }
    return 0;
}

/* cmp_str -- byte order, for the sorted listing below. */
static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

void osr_apply_verbs(Str *out) {
    Str dir;
    DIR *d;
    struct dirent *e;
    char **found = NULL;
    size_t count = 0;
    size_t cap = 0;
    size_t i;

    str_init(&dir);
    str_addz(&dir, osr_mod_root());
    str_addz(&dir, "/lib");
    d = opendir(str_text(&dir));
    if (d == NULL) { str_free(&dir); return; }

    while ((e = readdir(d)) != NULL) {
        Str path, current;
        char *buf;
        size_t len = 0;
        size_t pos = 0;
        size_t nl = strlen(e->d_name);
        Line line;

        if (nl < 3 || strcmp(e->d_name + nl - 2, ".c") != 0) continue;
        str_init(&path);
        str_add(&path, str_text(&dir), dir.len);
        str_addc(&path, '/');
        str_addz(&path, e->d_name);
        buf = slurp(str_text(&path), &len);
        str_free(&path);
        if (buf == NULL) continue;

        str_init(&current);
        while (next_line(buf, len, &pos, &line)) {
            Str name;
            str_init(&name);
            if (def_name(&name, &line)) {
                str_reset(&current);
                str_add(&current, str_text(&name), name.len);
            } else if (current.len > 0 &&
                       line_has_call(&line, "osr_theme_only(")) {
                /* This function asks before acting, so it is neutralized. */
                int seen = 0;
                for (i = 0; i < count && !seen; i++)
                    if (strcmp(found[i], str_text(&current)) == 0) seen = 1;
                if (!seen) {
                    if (count == cap) {
                        cap = cap ? cap * 2 : 16;
                        found = (char **)realloc(found, cap * sizeof(char *));
                        if (found == NULL) osr_die_oom();
                    }
                    found[count] = (char *)malloc(current.len + 1);
                    if (found[count] == NULL) osr_die_oom();
                    memcpy(found[count], str_text(&current), current.len + 1);
                    count++;
                }
                str_reset(&current);          /* one entry per function */
            }
            str_free(&name);
        }
        str_free(&current);
        free(buf);
    }
    closedir(d);
    str_free(&dir);

    /* Sorted, so the listing is the same on every filesystem. */
    qsort(found, count, sizeof(found[0]), cmp_str);
    for (i = 0; i < count; i++) {
        str_addz(out, found[i]);
        str_addc(out, '\n');
        free(found[i]);
    }
    free(found);
}

/* is_theme_module -- does this module name one of the markers anywhere? A
 * plain substring search over the whole file, which is what `grep -qE` with an
 * alternation of three literals amounts to. */
static int is_theme_module(const char *path) {
    char *buf;
    size_t len = 0;
    size_t i;
    int hit = 0;

    buf = slurp(path, &len);
    if (buf == NULL) return 0;
    for (i = 0; !hit && THEME_MARKERS[i] != NULL; i++)
        if (strstr(buf, THEME_MARKERS[i]) != NULL) hit = 1;
    free(buf);
    return hit;
}

/* cmp_name -- byte order, the collation a shell glob expands in under the C
 * locale the installer runs with. */
static int cmp_name(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* module_is_themable -- does this module carry a theme layer, whichever tier it
 * lives in? A .sh module answers the way lib/apply.sh asked -- the markers in
 * its own text, a grep that cannot go stale. A module that has moved to C has no
 * .sh to grep, so it answers from its registry row instead (lib/modules.c), and
 * test/unit/module_themable.sh is what keeps those two criteria agreeing. */
static int module_is_themable(const char *name) {
    Str path;
    int ok;

    str_init(&path);
    str_addz(&path, osr_mod_root());
    str_addz(&path, "/modules/");
    str_addz(&path, name);
    str_addz(&path, ".sh");
    ok = file_exists(str_text(&path)) ? is_theme_module(str_text(&path))
                                      : osr_module_themable(name);
    str_free(&path);
    return ok;
}

/* module_path -- os-rice/modules/<name>.sh */
static void module_path(Str *out, const char *name) {
    str_addz(out, osr_mod_root());
    str_addz(out, "/modules/");
    str_addz(out, name);
    str_addz(out, ".sh");
}

/* --- the apply itself ------------------------------------------------------
 *
 * lib/apply.sh's osr_apply_theme, in process. It lived in a lib rather than
 * inline in install.sh so it could be driven against a throwaway HOME, and that
 * is not a testing nicety: the runner resolves OSR_HOME from passwd, so a test
 * that sets OSR_HOME and then runs the runner writes to the REAL home of
 * whoever runs the suite. This entry point is what lets a test exercise the
 * path without that being possible.
 */

/* run_layer -- one module's theme pass, with its output on the run log.
 *
 * A single broken module must not abort a theme switch and leave the desktop
 * half-painted, so this forks: a module's osr_die kills only its layer. Its
 * chatter goes to $OSR_LOG rather than the terminal, because the interesting
 * output of a theme apply is the one success line. */
static int run_layer(const char *name) {
    pid_t pid;
    int status;

    fflush(stdout);
    fflush(stderr);
    pid = fork();
    if (pid < 0) return 0;
    if (pid == 0) {
        int fd = open(env_str("OSR_LOG", "/dev/null"), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) {
            (void)dup2(fd, 1);
            (void)dup2(fd, 2);
            if (fd > 2) close(fd);
        }
        _exit(osr_module_run(name, 1) ? 0 : 1);
    }
    if (waitpid(pid, &status, 0) < 0) return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int osr_apply_theme(const char *name) {
    Str rice, mods, msg;
    size_t pos = 0;
    size_t total = 0, n = 0;
    Line line;
    char num[32];

    osr_resolve_theme(name);

    /* Neutralize every install/build/download/service verb for the rest of this
     * process, then run the same modules a rice install runs. What is left of
     * them is the file copying. */
    osr_set_theme_only(1);

    str_init(&rice);
    osr_state_get(&rice, "rice");
    setenv("OSR_RICE", str_text(&rice), 1);
    if (rice.len > 0) {
        Str dir;
        str_init(&dir);
        str_addz(&dir, env_str("OSR_ROOT", "."));
        str_addz(&dir, "/rices/");
        str_addz(&dir, str_text(&rice));
        setenv("OSR_RICE_DIR", str_text(&dir), 1);
        str_free(&dir);
    }

    str_init(&mods);
    osr_theme_modules(&mods, str_text(&rice));
    while (next_line(str_text(&mods), mods.len, &pos, &line))
        if (line.len > 0) total++;

    sprintf(num, "%lu", (unsigned long)total);
    setenv("OSR_STEP_TOTAL", num, 1);
    setenv("OSR_STEP_N", "0", 1);

    str_init(&msg);
    str_addz(&msg, "applying theme '");
    str_addz(&msg, env_str("OSR_THEME", ""));
    str_addc(&msg, '\'');
    if (rice.len > 0) {
        str_addz(&msg, " over rice '");
        str_addz(&msg, str_text(&rice));
        str_addc(&msg, '\'');
    }
    str_addz(&msg, " (");
    str_addl(&msg, (long)total);
    str_addz(&msg, " layers)");
    osr_info(str_text(&msg));
    str_free(&msg);

    pos = 0;
    while (next_line(str_text(&mods), mods.len, &pos, &line)) {
        Str mod;
        if (line.len == 0) continue;
        n++;
        sprintf(num, "%lu", (unsigned long)n);
        setenv("OSR_STEP_N", num, 1);
        str_init(&mod);
        str_add(&mod, line.start, line.len);
        {
            char prefix[32];
            osr_debugf("%slayer: %s", osr_step_prefix(prefix, sizeof(prefix)),
                       str_text(&mod));
        }
        if (!run_layer(str_text(&mod)))
            osr_warnf("layer '%s' failed - skipped (see %s)",
                      str_text(&mod), env_str("OSR_LOG", "the run log"));
        str_free(&mod);
    }
    str_free(&mods);

    (void)osr_apply_theme_configs();
    (void)osr_apply_wallpaper();
    (void)osr_state_set("theme", env_str("OSR_THEME", ""));
    {
        char stamp[32];
        sprintf(stamp, "%ld", (long)time(NULL));
        (void)osr_state_set("applied", stamp);
    }
    str_free(&rice);
    return 1;
}

void osr_theme_modules(Str *out, const char *rice) {
    Str list;
    char *buf = NULL;
    size_t len = 0;

    str_init(&list);
    if (rice != NULL && rice[0] != '\0') {
        str_addz(&list, osr_mod_root());
        str_addz(&list, "/rices/");
        str_addz(&list, rice);
        str_addz(&list, "/rice.list");
        buf = slurp(str_text(&list), &len);
    }

    if (buf != NULL) {
        Str lines;
        size_t pos = 0;
        Line line;

        /* The manifest's directive lines, comments and blanks already gone --
         * the same `osr theme lines` the shell tier piped into `read`. */
        str_init(&lines);
        osr_theme_read_lines(&lines, str_text(&list));
        free(buf);

        while (next_line(str_text(&lines), lines.len, &pos, &line)) {
            Str name;
            if (memchr(line.start, ':', line.len) != NULL)
                continue;   /* require: / theme: / themes: -- not modules */
            str_init(&name);
            str_add(&name, line.start, line.len);
            if (module_is_themable(str_text(&name))) {
                str_add(out, str_text(&name), name.len);
                str_addc(out, '\n');
            }
            str_free(&name);
        }
        str_free(&lines);
    } else {
        /* No recorded rice (first run, or a hand-built system): every module
         * that can paint something. */
        Str dir;
        DIR *d;
        char **names = NULL;
        size_t n = 0;
        size_t cap = 0;
        size_t i;

        str_init(&dir);
        str_addz(&dir, osr_mod_root());
        str_addz(&dir, "/modules");
        d = opendir(str_text(&dir));
        if (d != NULL) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL) {
                size_t nl = strlen(e->d_name);
                if (nl < 4 || strcmp(e->d_name + nl - 3, ".sh") != 0) continue;
                if (n == cap) {
                    cap = cap ? cap * 2 : 32;
                    names = (char **)realloc(names, cap * sizeof *names);
                    if (names == NULL) break;
                }
                names[n] = (char *)malloc(nl + 1);
                if (names[n] == NULL) break;
                memcpy(names[n], e->d_name, nl + 1);
                n++;
            }
            closedir(d);
        }
        /* The C tier's names go into the same sorted sweep: modules' *.sh were
         * the whole world when lib/apply.sh wrote this, and a module that has
         * moved to C still paints. */
        {
            Str reg;
            size_t pos = 0;
            Line l;
            str_init(&reg);
            osr_module_names(&reg);
            while (next_line(str_text(&reg), reg.len, &pos, &l)) {
                if (n == cap) {
                    cap = cap ? cap * 2 : 32;
                    names = (char **)realloc(names, cap * sizeof *names);
                    if (names == NULL) break;
                }
                names[n] = (char *)malloc(l.len + 4);
                if (names[n] == NULL) break;
                memcpy(names[n], l.start, l.len);
                memcpy(names[n] + l.len, ".sh", 4);   /* the same ".sh" tail */
                n++;
            }
            str_free(&reg);
        }
        /* readdir order is the filesystem's; the glob the shell expanded was
         * sorted, and the module order decides the order layers land in. */
        if (names != NULL) qsort(names, n, sizeof *names, cmp_name);

        for (i = 0; i < n; i++) {
            Str name;
            str_init(&name);
            str_add(&name, names[i], strlen(names[i]) - 3);
            if (module_is_themable(str_text(&name)))
                { str_add(out, str_text(&name), name.len); str_addc(out, '\n'); }
            str_free(&name);
            free(names[i]);
        }
        free(names);
        str_free(&dir);
    }
    str_free(&list);
}

static int apply_usage(void) {
    fputs("usage: osr apply <subcommand> [args]\n\n", stderr);
    fputs("  verbs               every mutating verb a theme apply neutralizes\n", stderr);
    fputs("  modules [rice]      the modules that carry a theme layer\n", stderr);
    fputs("  theme [name]        apply a theme only (SS6a): layers, no packages\n", stderr);
    return 2;
}

int osr_apply_main(int argc, char **argv) {
    Str out;

    if (argc < 2) return apply_usage();

    str_init(&out);
    if (strcmp(argv[1], "verbs") == 0 && argc == 2) {
        osr_apply_verbs(&out);
    } else if (strcmp(argv[1], "modules") == 0 && argc <= 3) {
        osr_theme_modules(&out, argc == 3 ? argv[2] : "");
    } else if (strcmp(argv[1], "theme") == 0 && argc <= 3) {
        /* The whole §6a apply. `osr theme <name>` is this, and so is the
         * runner's --theme-only path. */
        str_free(&out);
        return osr_apply_theme(argc == 3 ? argv[2] : "") ? 0 : 1;
    } else {
        str_free(&out);
        return apply_usage();
    }
    fwrite(str_text(&out), 1, out.len, stdout);
    str_free(&out);
    return 0;
}
