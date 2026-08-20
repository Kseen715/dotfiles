/* lib/modules.c -- the registry of Linux modules written in C.
 *
 * A rice manifest names modules and install.sh runs each one. A module is
 * either a shell script under modules (the ~118 that exist today) or a C
 * function registered here (modules/<name>.c, the POSIX branch of it where
 * the file also has a Windows one). install.sh asks `osr module has <name>`
 * and, when the answer is yes, runs `osr module run <name>` instead of
 * sourcing the script -- so the two kinds coexist and a rice.list never has
 * to know which is which.
 *
 * Writing one in C buys what the sh tier cannot have: `osr_step` can
 * fork a FUNCTION of this program, where the shell run_step could
 * only fork a shell function. Everything a module may call is lib/module.h.
 *
 * The `session` field is the C form of a .sh module's `# session:` first line
 * (x11 / wayland / x11+wayland), which is what lets `grep -l` answer "what
 * breaks if I move this rice to Wayland" without reading every module.
 *
 *   osr module list           every C module, one per line
 *   osr module has <name>     exit 0 when this tier owns that name
 *   osr module session <name> its session marker
 *   osr module themable <name>  exit 0 when it consumes the theme
 *   osr module run <name>     install it
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

#include "common.h"
#include "cmds.h"
#include "module.h"

/* One row per module. Keep it alphabetical: it is also the listing order. */
typedef struct {
    const char *name;
    const char *session;
    int themable;              /* does it read the resolved theme at all? */
    int (*run)(void);
} ModuleRow;

int osrm_docker(void);
int osrm_fastfetch(void);
int osrm_flameshot(void);
int osrm_tcc(void);

static const ModuleRow modules[] = {
    { "docker",    "x11+wayland", 0, osrm_docker },
    { "fastfetch", "x11+wayland", 1, osrm_fastfetch },
    { "flameshot", "x11",         0, osrm_flameshot },
    { "tcc",       "x11+wayland", 0, osrm_tcc }
};
#define MODULE_COUNT (sizeof(modules) / sizeof(modules[0]))

static const ModuleRow *find(const char *name);

/* osr_module_themable -- "does installing this module need a theme?".
 *
 * Only a module that reads $OSR_THEME/$OSR_THEME_DIR has anything to do with
 * the answer, so only those make install.sh ask the question. Asking it for
 * `osr module benchmark` -- a package install with no appearance at all -- put
 * a theme picker in front of a benchmark, which is what this exists to stop.
 *
 * A C module carries the flag in its row. A .sh module carries it as the
 * `# themable: yes` header beside `# session:`, and the marker is authoritative
 * rather than inferred: test/unit/module_themable.sh diffs every marker against
 * what the script actually references, so a module that grows a theme layer and
 * forgets the header fails the suite instead of silently losing its paint. */
int osr_module_themable(const char *name) {
    Str path;
    char *buf;
    size_t len, pos = 0;
    Line line;
    int themable = 0;
    const ModuleRow *m;

    m = find(name);
    if (m != NULL) return m->themable;

    str_init(&path);
    str_addz(&path, env_str("OSR_ROOT", "."));
    str_addz(&path, "/modules/");
    str_addz(&path, name);
    str_addz(&path, ".sh");
    buf = slurp(str_text(&path), &len);
    str_free(&path);
    if (buf == NULL) return 0;

    /* The header block only: a `# themable:` further down is prose, not a
     * marker, and the whole point is that the answer is cheap to find. */
    while (next_line(buf, len, &pos, &line)) {
        if (line.len == 0 || line.start[0] != '#') break;
        if (line.len > 11 && memcmp(line.start, "# themable:", 11) == 0) {
            const char *v = line.start + 11;
            size_t n = line.len - 11;
            while (n > 0 && is_space(*v)) { v++; n--; }
            themable = (n >= 3 && memcmp(v, "yes", 3) == 0);
            break;
        }
    }
    free(buf);
    return themable;
}

/* osr_module_names -- every C module's name, appended to out one per line.
 * install.sh's `--list-modules` merges these with the shell scripts. */
void osr_module_names(Str *out) {
    size_t i;
    for (i = 0; i < MODULE_COUNT; i++) {
        str_addz(out, modules[i].name);
        str_addc(out, '\n');
    }
}

static const ModuleRow *find(const char *name) {
    size_t i;
    for (i = 0; i < MODULE_COUNT; i++) {
        if (strcmp(modules[i].name, name) == 0) return &modules[i];
    }
    return NULL;
}

static int usage(void) {
    fputs("usage: osr module <subcommand> [name]\n\n", stderr);
    fputs("  list              every module this tier implements\n", stderr);
    fputs("  has <name>        exit 0 when it does implement <name>\n", stderr);
    fputs("  session <name>    its `# session:` marker\n", stderr);
    fputs("  themable <name>   exit 0 when it consumes the resolved theme\n", stderr);
    fputs("  run <name>        install it\n", stderr);
    fputs("  pkgmap <name>     what lib/pkgmap resolves that name to\n", stderr);
    return 2;
}

int osr_module_main(int argc, char **argv) {
    if (argc < 2) return usage();

    if (strcmp(argv[1], "list") == 0 && argc == 2) {
        Str out;
        str_init(&out);
        osr_module_names(&out);
        out_flush(&out);
        str_free(&out);
        return 0;
    }
    if (strcmp(argv[1], "pkgmap") == 0 && argc == 3) {
        /* the resolver by itself, so a test can diff it against pkg.sh's
         * _pkgmap_one without installing anything */
        Str out;
        str_init(&out);
        osr_pkgmap_resolve(&out, argv[2]);
        out_flush(&out);
        str_free(&out);
        return 0;
    }
    if (strcmp(argv[1], "has") == 0 && argc == 3) {
        return find(argv[2]) != NULL ? 0 : 1;
    }
    if (strcmp(argv[1], "session") == 0 && argc == 3) {
        const ModuleRow *m = find(argv[2]);
        if (m == NULL) return 1;
        printf("%s\n", m->session);
        return 0;
    }
    if (strcmp(argv[1], "themable") == 0 && argc == 3) {
        return osr_module_themable(argv[2]) ? 0 : 1;
    }
    if (strcmp(argv[1], "run") == 0 && argc == 3) {
        const ModuleRow *m = find(argv[2]);
        if (m == NULL) {
            osr_warn("no such C module");
            return 1;
        }
        /* A failing module is reported and the run continues -- one broken
         * module must not abort a whole rice install, same contract the sh
         * run_module has. */
        return m->run() ? 0 : 1;
    }
    return usage();
}
