/* lib/apply.c -- the portable half of lib/apply.sh. See lib/apply.h.
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "module.h"

#include "apply.h"
#include "cmds.h"

/* Libs whose functions all mutate the system: neutralized wholesale. */
static const char *const MUTATING_LIBS[] = {
    "pkg", "build", "net", "git", "service", "fonts", NULL
};

/* The read-only exceptions. _pkgmap_one is a query, and so is everything it
 * calls: the helpers behind the facet ladder (_pkgmap_exact/_pkgmap_range/
 * _pkgmap_rhs/_pkgmap_re and the _ver_* arithmetic) read map files and compare
 * strings, nothing else. Stubbing one of them leaves _pkgmap_one resolving
 * every name to the empty string. */
static const char *const QUERY_OK[] = {
    "pkg_installed", "_pkgmap_one", "_pkgmap_exact", "_pkgmap_range",
    "_pkgmap_rhs", "_pkgmap_re", "_ver_cmp", "_ver_match", "_ver_prefixes",
    "_spec_method", "_native_installed", "_native_held", "service_resolve",
    "osr_downloader", "_chafa_version", "_chafa_ok", "_fzf_version",
    "_fzf_ok", "_osr_pkgconfig_path", "_yb_deb_url", NULL
};

/* The markers that make a module theme-carrying. */
static const char *const THEME_MARKERS[] = {
    "OSR_THEME_DIR", "install_theme_layer", "osr_theme_source", NULL
};

int osr_apply_is_query(const char *name) {
    size_t i;
    for (i = 0; QUERY_OK[i] != NULL; i++)
        if (strcmp(QUERY_OK[i], name) == 0) return 1;
    return 0;
}

/* def_name -- the function name a line defines, sh's
 * one-line sed: an unindented lowercase identifier followed immediately by
 * "()". Anything else is not a definition. */
static int def_name(Str *out, const Line *line) {
    const char *p = line->start;
    const char *end = line->start + line->len;
    const char *start = p;

    if (p >= end) return 0;
    if (!((*p >= 'a' && *p <= 'z') || *p == '_')) return 0;
    while (p < end && ((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
                       *p == '_')) p++;
    if (end - p < 2 || p[0] != '(' || p[1] != ')') return 0;
    str_add(out, start, (size_t)(p - start));
    return 1;
}

void osr_apply_verbs(Str *out) {
    size_t i;
    for (i = 0; MUTATING_LIBS[i] != NULL; i++) {
        Str path;
        char *buf;
        size_t len = 0;
        size_t pos = 0;
        Line line;

        str_init(&path);
        str_addz(&path, osr_mod_root());
        str_addz(&path, "/lib/");
        str_addz(&path, MUTATING_LIBS[i]);
        str_addz(&path, ".sh");
        buf = slurp(str_text(&path), &len);
        str_free(&path);
        if (buf == NULL) continue;   /* [ -f ... ] || continue */

        while (next_line(buf, len, &pos, &line)) {
            if (def_name(out, &line)) str_addc(out, '\n');
        }
        free(buf);
    }
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
            Str path;
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
        /* The C tier's names go into the same sorted sweep: `modules/*.sh` was
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
    } else {
        str_free(&out);
        return apply_usage();
    }
    fwrite(str_text(&out), 1, out.len, stdout);
    str_free(&out);
    return 0;
}
