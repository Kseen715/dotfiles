/* test/unit_c/install_test.c -- the runner: `osr install`, and the manifest it
 * runs.
 *
 * This is the orchestration everything else hangs off. It parses the option
 * loop, reads a rice.list, resolves the user and the theme, checks the
 * preconditions, runs the modules in order, and writes the state at the end.
 *
 * THE MANIFEST IS HAND-WRITTEN, AND THAT IS THE POINT (Decisions: no TOML). A
 * newline list with `#` comments is more readable than a table and free to
 * parse -- but only if the parser survives what people actually type. So the
 * fixture rice below is deliberately hostile: comments in every position,
 * tabs, trailing whitespace, blank lines, `theme:`/`themes:`/`require:`
 * directives mixed in with module names, and a last line with no newline. A
 * directive that leaked into the module list would try to install a module
 * called `theme:`; a module lost to a stray tab would silently not run.
 *
 * ORDER IS THE DEPENDENCY GRAPH (Decisions: no auto-resolved DAG). The
 * manifest's order is the order modules run in, so the step counter and the
 * sequence below are asserting the one guarantee a rice author has.
 *
 * Hermetic: a fixture tree of stub modules and rices, a sandbox $HOME resolved
 * through a fixture passwd file, and $PATH reduced to stubs -- so no module
 * installs anything and the runner never touches the real home of whoever runs
 * the suite.
 *
 * Replaces test/unit/install_c_parity.sh. See test/harness.h.
 */
#include "../harness.c"

static OsrSandbox sb;

static const char *at(const char *rel) {
    static HStr ring[4];
    static int ready = 0;
    static int next = 0;
    HStr *p;
    if (!ready) { int i; for (i = 0; i < 4; i++) hs_init(&ring[i]); ready = 1; }
    p = &ring[next];
    next = (next + 1) % 4;
    hs_path(p, hs_text(&sb.root), rel);
    return hs_text(p);
}

/* run -- `install.sh <args>`, the shim people actually type, out of a fixture
 * tree rather than the real one. Up to four arguments, which is every shape
 * the option loop is exercised with below. */
static int run(const char *a, const char *b, const char *c, const char *d,
               const char *e) {
    char *argv[8];
    int n = 0;
    HStr script;
    int rc;

    hs_init(&script);
    hs_path(&script, hs_text(&sb.root), "tree/install.sh");
    argv[n++] = (char *)"/bin/sh";
    argv[n++] = (char *)hs_text(&script);
    if (a != NULL) argv[n++] = (char *)a;
    if (b != NULL) argv[n++] = (char *)b;
    if (c != NULL) argv[n++] = (char *)c;
    if (d != NULL) argv[n++] = (char *)d;
    if (e != NULL) argv[n++] = (char *)e;
    argv[n] = NULL;

    osr_sb_rm(&sb, "home");
    osr_sb_mkdir(&sb, "home");
    osr_sb_reset(&sb);
    rc = h_run(&sb, argv);
    hs_free(&script);
    return rc;
}

static void said(const char *needle, const char *label) {
    osr_assert_true(strstr(osr_sb_capture_both(&sb), needle) != NULL, label);
}
static void quiet_about(const char *needle, const char *label) {
    osr_assert_true(strstr(osr_sb_capture_both(&sb), needle) == NULL, label);
}

/* order -- did `first` appear before `second` in the run's output? */
static int order(const char *first, const char *second) {
    const char *out = osr_sb_capture_both(&sb);
    const char *a = strstr(out, first);
    const char *b = strstr(out, second);
    return a != NULL && b != NULL && a < b;
}

int main(void) {
    HStr p;

    osr_sb_init(&sb);
    hs_init(&p);

    /* --- the fixture tree ---------------------------------------------
     *
     * install.sh and osr are copied rather than symlinked because install.sh
     * delegates to ./osr, which is where the single build/osr resolution
     * lives -- the shim would otherwise exec into nothing. */
    osr_sb_mkdir(&sb, "tree/rices/demo");
    osr_sb_mkdir(&sb, "tree/rices/broken");
    osr_sb_mkdir(&sb, "tree/modules");
    {
        static const struct { const char *from; const char *to; } copies[] = {
            { "install.sh", "tree/install.sh" },
            { "osr",        "tree/osr" }
        };
        size_t i;
        for (i = 0; i < sizeof(copies) / sizeof(copies[0]); i++) {
            HStr src;
            char *text;
            hs_init(&src);
            hs_path(&src, hs_text(&sb.osr_root), copies[i].from);
            text = h_slurp(hs_text(&src));
            osr_sb_write(&sb, copies[i].to, text, 0755);
            free(text);
            hs_free(&src);
        }
    }
    {
        /* lib/, build/ and themes/ are the real ones: what is being exercised
         * is the runner, not a reimplementation of the tree. */
        static const struct { const char *target; const char *link; } links[] = {
            { "lib", "tree/lib" }, { "build", "tree/build" },
            { "themes", "tree/themes" }
        };
        size_t i;
        for (i = 0; i < sizeof(links) / sizeof(links[0]); i++) {
            HStr t, l;
            hs_init(&t); hs_init(&l);
            hs_path(&t, hs_text(&sb.osr_root), links[i].target);
            hs_path(&l, hs_text(&sb.root), links[i].link);
            unlink(hs_text(&l));
            if (symlink(hs_text(&t), hs_text(&l)) != 0)
                osr_fail("fixture: symlink", hs_text(&l));
            hs_free(&t); hs_free(&l);
        }
    }

    /* The hostile manifest. Every line here is a shape somebody has typed.
     * The require: predicates are ones every host satisfies, so the run
     * reaches the modules rather than stopping in preflight -- which
     * predicate FAILS is preflight_test.c's question, not this one. */
    osr_sb_write(&sb, "tree/rices/demo/rice.list",
        "# the demo rice\n"
        "theme: nord\n"
        "themes: nord xin\n"
        "\n"
        "demo-one   \n"
        "\t demo-two\t\n"
        "require: cmd:sh\n"
        "demo-three # trailing comment\n"
        "#full comment\n"
        "require: cmd:sed\n"
        "demo-last", 0644);
    /* A rice directory with no rice.list in it. */
    osr_sb_write(&sb, "tree/rices/broken/nothing-here", "demo-one\n", 0644);

    {
        static const char *const mods[] = {
            "demo-one", "demo-two", "demo-three", "demo-last", "other", NULL
        };
        int i;
        for (i = 0; mods[i] != NULL; i++) {
            HStr rel, body;
            hs_init(&rel); hs_init(&body);
            hs_add(&rel, "tree/modules/");
            hs_add(&rel, mods[i]);
            hs_add(&rel, ".sh");
            hs_add(&body, "# session: x11\n# themable: yes\n");
            hs_add(&body, "printf \"STUB module ");
            hs_add(&body, mods[i]);
            hs_add(&body, " ran\\n\"\n");
            osr_sb_write(&sb, hs_text(&rel), hs_text(&body), 0644);
            hs_free(&rel); hs_free(&body);
        }
    }

    /* The account being riced is resolved through a fixture passwd file, so
     * the runner cannot reach the real home of whoever runs the suite. */
    {
        HStr line;
        hs_init(&line);
        hs_add(&line, "tester:x:1000:1000::");
        hs_add(&line, hs_text(&sb.home));
        hs_add(&line, ":/bin/sh\n");
        osr_sb_write(&sb, "etc/passwd", hs_text(&line), 0644);
        hs_free(&line);
    }
    osr_sb_env(&sb, "OSR_PASSWD_FILE", at("etc/passwd"));
    osr_sb_env(&sb, "OSR_SHELLS_FILE", at("etc/shells"));
    osr_sb_env(&sb, "USER", "tester");
    osr_sb_env(&sb, "SUDO_USER", "");
    /* The runner resolves everything else itself, so nothing is pre-set. */
    osr_sb_env(&sb, "OSR_ROOT", "");
    osr_sb_env(&sb, "OSR_LIB", "");
    osr_sb_env(&sb, "OSR_HOME", "");

    /* ================================================================
     * 1. The listings and the help
     * ================================================================ */
    osr_assert_rc(run("--help", NULL, NULL, NULL, NULL), 0, "--help exits 0");
    said("Usage:", "--help prints its usage");
    osr_assert_rc(run("-h", NULL, NULL, NULL, NULL), 0, "-h is the same");
    said("Usage:", "-h prints the same usage");

    osr_assert_rc(run("--list", NULL, NULL, NULL, NULL), 0, "--list exits 0");
    said("demo", "--list names the rices in the tree");
    quiet_about("broken",
        "--list names only directories that carry a rice.list -- a directory "
        "without one is not a rice yet, and offering it would mean offering "
        "something that cannot install");

    osr_assert_rc(run("--list-themes", NULL, NULL, NULL, NULL), 0,
                  "--list-themes exits 0");
    said("nord", "--list-themes names the themes");

    osr_assert_rc(run("--list-modules", NULL, NULL, NULL, NULL), 0,
                  "--list-modules exits 0");
    said("demo-one", "--list-modules names the tree's own modules");
    said("fastfetch",
        "--list-modules also names the core's COMPILED-IN registry -- the two "
        "tiers coexist, and a listing that showed only the directory would "
        "have gone silently empty as modules moved to C");

    /* ================================================================
     * 2. Every error path
     *
     * All of these are user input, so all of them have to say what was wrong
     * rather than fall through into an install.
     * ================================================================ */
    osr_assert_true(run("--nope", NULL, NULL, NULL, NULL) != 0,
                    "an unknown option fails");
    said("--nope", "and names the option");
    osr_assert_true(run("-x", NULL, NULL, NULL, NULL) != 0,
                    "an unknown short option fails");
    osr_assert_true(run(NULL, NULL, NULL, NULL, NULL) != 0,
                    "no rice at all fails rather than picking one");
    osr_assert_true(run("demo", "other", NULL, NULL, NULL) != 0,
                    "two rices fails -- a run installs one rice, and guessing "
                    "which would be worse than asking");
    osr_assert_true(run("nosuchrice", NULL, NULL, NULL, NULL) != 0,
                    "a rice that does not exist fails");
    osr_assert_true(run("broken", NULL, NULL, NULL, NULL) != 0,
                    "a rice directory with no rice.list fails");
    osr_assert_true(run("--module", NULL, NULL, NULL, NULL) != 0,
                    "--module with no module names fails");
    osr_assert_true(run("--module", "nosuchmodule", NULL, NULL, NULL) != 0,
                    "--module with a module that does not exist fails");

    /* An option that takes an operand, given none. */
    osr_assert_rc(run("--user", NULL, NULL, NULL, NULL), 1,
                  "an option missing its operand exits 1");
    said("user needs a name",
        "and says which option was missing what -- the shell original died "
        "inside its own ${x:?...} expansion and exited 2, which told the user "
        "nothing");

    /* ================================================================
     * 3. A whole run, and the manifest it parsed
     * ================================================================ */
    osr_assert_rc(run("demo", NULL, NULL, NULL, NULL), 0,
                  "a plain rice install succeeds");

    said("STUB module demo-one ran", "manifest: the first module ran");
    said("STUB module demo-two ran",
        "manifest: and the one written with a leading tab and a trailing tab");
    said("STUB module demo-three ran",
        "manifest: and the one with a trailing comment after its name");
    said("STUB module demo-last ran",
        "manifest: and the last line, which has no trailing newline at all");
    quiet_about("STUB module other ran",
        "manifest: a rice runs ONLY its own modules -- `other` exists in the "
        "tree and is not in this rice");

    quiet_about("module: theme",
        "manifest: `theme:` is a directive, not a module named theme:");
    quiet_about("module: themes",
        "manifest: nor is `themes:`");
    said("require cmd:sh",
        "manifest: a require: line reaches preflight");
    said("require cmd:sed",
        "manifest: and so does the second one");

    osr_assert_true(order("demo-one", "demo-two") &&
                    order("demo-two", "demo-three") &&
                    order("demo-three", "demo-last"),
        "manifest: the modules run in the order the manifest lists them -- "
        "that order IS the dependency graph, which is why there is no DAG");

    said("[01/04] module: demo-one",
        "the step counter counts the modules the manifest yielded, so a "
        "directive that leaked in would show up as a wrong total");
    said("theme: nord",
        "the rice's own `theme:` line is the default when none is asked for");

    /* A .sh module still runs, and a rice.list never says which tier it
     * wanted. What such a module no longer gets is os-rice's own verbs:
     * pkg_install and the rest are C functions now, so a shell module runs
     * with the facts in its environment and plain sh. Enough for a one-off
     * local module; a real one is written in C. */
    said("STUB module demo-one ran",
        "a shell module still runs through the C runner -- nothing in a "
        "rice.list says which tier a module is written in");

    /* The state is written at the end, and it records both axes. */
    {
        HStr st;
        char *text;
        hs_init(&st);
        hs_path(&st, hs_text(&sb.root), "home/.config/osr/state");
        text = h_slurp(hs_text(&st));
        osr_assert_true(strstr(text, "rice=demo") != NULL,
            "the run records which rice was installed");
        osr_assert_true(strstr(text, "theme=") != NULL,
            "and which theme, so a later `osr apply theme` has somewhere to "
            "start from");
        free(text);
        hs_free(&st);
    }

    /* ================================================================
     * 4. The option loop
     *
     * No probe into the runner's variables: what it parsed is visible in what
     * it then did, which is the thing that actually matters.
     * ================================================================ */
    osr_assert_rc(run("--user", "tester", "--theme", "xin", "demo"), 0,
                  "every option together parses");
    said("theme: xin",
        "--theme overrides the rice's own default");

    osr_assert_rc(run("demo", "--user", "tester", "--verbose", NULL), 0,
                  "options AFTER the rice name parse too -- people type them "
                  "in that order and a loop that stopped at the first operand "
                  "would silently ignore them");

    osr_assert_rc(run("--module", "demo-one", "demo-two", NULL, NULL), 0,
                  "--module runs a list of modules with no rice at all");
    said("STUB module demo-one ran", "--module: the named modules run");
    said("STUB module demo-two ran", "--module: both of them");
    quiet_about("STUB module demo-three ran",
        "--module: and nothing else -- this is the verb for reinstalling one "
        "thing without touching the rest of a box");

    osr_assert_rc(run("--theme-only", "--theme", "nord", "--no-reload", NULL), 0,
                  "--theme-only runs with no rice named");
    quiet_about("Installing",
        "--theme-only installs nothing -- it is the same engine with every "
        "mutating verb neutralised");

    /* ================================================================
     * 5. switch is the same engine, with a different closing line
     * ================================================================ */
    osr_sb_env(&sb, "OSR_MODE", "switch");
    osr_assert_rc(run("demo", NULL, NULL, NULL, NULL), 0, "a switch succeeds");
    said("switched to rice",
        "switch: the closing line says a switch happened rather than an "
        "install -- packages are accreted and only the theme layers are "
        "replaced, and telling the user otherwise would be a lie");
    said("STUB module demo-one ran",
        "switch: and it is the same engine underneath -- the modules run");
    osr_sb_env(&sb, "OSR_MODE", "");

    hs_free(&p);
    osr_sb_free(&sb);
    return osr_finish();
}
