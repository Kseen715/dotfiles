/* test/unit_c/apply_test.c -- the two lists lib/apply.c derives from the tree:
 * every mutating verb a theme apply neutralizes, and the modules that carry a
 * theme layer.
 *
 * WHY THIS ONE ASSERTS PROPERTIES AND NOT CONTENTS
 *
 * Both lists are read out of the real tree, and that is the whole point of
 * them: they cannot drift from the sources. So there is no fixed list to
 * compare against -- any copy of one would be wrong the day a module or a
 * rice is added, and a test that has to be updated on every ordinary change
 * is a test nobody reads. What holds regardless of what the tree contains is
 * asserted instead: the list is the real one and not a stub, it contains the
 * verbs it must, it is sorted, every name in it is a module, and the
 * whole-tree scan overshoots the per-rice list rather than under-painting.
 *
 * Was test/unit/apply_c_parity.sh. See DESIGN 13.
 */
#include "../harness.c"

static OsrSandbox sb;

/* lines -- how many non-empty lines a captured list has. */
static int lines(const char *text) {
    int n = 0;
    const char *p = text;
    while (*p != '\0') {
        const char *nl = strchr(p, '\n');
        size_t len = nl != NULL ? (size_t)(nl - p) : strlen(p);
        if (len > 0) n++;
        if (nl == NULL) break;
        p = nl + 1;
    }
    return n;
}

/* has_line -- an exact line, not a substring: `pkg_install` must be its own
 * entry and not merely part of `pkg_install_optional`. */
static int has_line(const char *text, const char *want) {
    const char *p = text;
    size_t wl = strlen(want);
    while (*p != '\0') {
        const char *nl = strchr(p, '\n');
        size_t len = nl != NULL ? (size_t)(nl - p) : strlen(p);
        if (len == wl && strncmp(p, want, wl) == 0) return 1;
        if (nl == NULL) break;
        p = nl + 1;
    }
    return 0;
}

/* sorted -- is the list in LC_ALL=C order? The scan promises it, and a caller
 * comparing two scans by string equality depends on it. */
static int sorted(const char *text) {
    const char *p = text;
    HStr prev;
    int ok = 1;
    hs_init(&prev);
    while (*p != '\0') {
        const char *nl = strchr(p, '\n');
        size_t len = nl != NULL ? (size_t)(nl - p) : strlen(p);
        if (len > 0) {
            HStr cur;
            hs_init(&cur);
            {
                size_t i;
                for (i = 0; i < len; i++) hs_addc(&cur, p[i]);
            }
            if (prev.len > 0 && strcmp(hs_text(&prev), hs_text(&cur)) > 0) ok = 0;
            hs_reset(&prev);
            hs_add(&prev, hs_text(&cur));
            hs_free(&cur);
        }
        if (nl == NULL) break;
        p = nl + 1;
    }
    hs_free(&prev);
    return ok;
}

/* grab -- run the core and keep what it printed, so the next run does not
 * invalidate it. The caller frees. */
static char *grab(const char *a, const char *b, const char *c) {
    HStr held;
    osr_sb_run_core(&sb, a, b, c, (const char *)NULL);
    hs_init(&held);
    hs_add(&held, osr_sb_capture(&sb));
    return held.p != NULL ? held.p : NULL;
}

int main(void) {
    char *verbs;
    char *all;
    char *again;
    char *unknown;

    osr_sb_init(&sb);
    /* This test reads the real tree rather than a sandbox: the lists ARE the
     * tree. Only $PATH and the palette are controlled. */

    /* --- 1. the mutating verb list ---------------------------------------- */
    verbs = grab("apply", "verbs", NULL);

    /* Non-vacuous first: every assertion below would pass against an empty
     * list, and an empty list is precisely the failure that would leave a
     * theme apply installing packages. */
    osr_assert_true(lines(verbs) > 50, "the verb list is the real one");
    osr_assert_true(has_line(verbs, "pkg_install"), "a package verb is in it");
    osr_assert_true(has_line(verbs, "enable_service"), "a service verb is in it");

    /* The read-only allowlist is the one thing that must NOT be stubbed:
     * every name on it has to be a real function of a mutating lib, or the
     * exception silently protects nothing. */
    osr_assert_true(has_line(verbs, "pkg_installed"),
                    "the query 'pkg_installed' is defined by a mutating lib");
    osr_assert_true(has_line(verbs, "_pkgmap_one"),
                    "the query '_pkgmap_one' is defined by a mutating lib");
    osr_assert_true(has_line(verbs, "service_resolve"),
                    "the query 'service_resolve' is defined by a mutating lib");
    osr_assert_true(has_line(verbs, "osr_downloader"),
                    "the query 'osr_downloader' is defined by a mutating lib");
    free(verbs);

    /* --- 2. theme-carrying modules ---------------------------------------- */
    all = grab("apply", "modules", NULL);
    osr_assert_true(lines(all) > 10, "the whole-tree scan is the real one");
    osr_assert_true(sorted(all), "the scan is in sorted order");

    again = grab("apply", "modules", "");
    osr_assert_eq(all, again, "an empty rice name is the same as none");
    free(again);

    /* An unknown rice falls back to the whole-tree scan, not to nothing:
     * under-painting is the failure mode this list exists to avoid. */
    unknown = grab("apply", "modules", "nosuchrice");
    osr_assert_eq(all, unknown, "an unknown rice falls back to the whole-tree scan");
    free(unknown);

    /* Every rice in the tree, so a manifest with require:/theme:/themes: rows
     * and a manifest without them are both covered. The per-rice list must be
     * non-empty and must not name anything the whole-tree scan missed --
     * overshooting is allowed, under-painting is the defect. */
    {
        HStr rices;
        char *listing;
        const char *p;
        hs_init(&rices);
        /* `osr install list-rices` is the listing verb; it indents each
         * name by two spaces, which the loop below trims. */
        listing = grab("install", "list-rices", NULL);
        p = listing;
        while (*p != '\0') {
            const char *nl = strchr(p, '\n');
            size_t len = nl != NULL ? (size_t)(nl - p) : strlen(p);
            while (len > 0 && (*p == ' ' || *p == '\t')) {
                p++;
                len--;
            }
            if (len > 0) {
                HStr rice;
                HStr label;
                char *mods;
                hs_init(&rice);
                hs_init(&label);
                {
                    size_t i;
                    for (i = 0; i < len; i++) hs_addc(&rice, p[i]);
                }
                mods = grab("apply", "modules", hs_text(&rice));
                hs_add(&label, "rice '");
                hs_add(&label, hs_text(&rice));
                hs_add(&label, "': it found some theme-carrying modules");
                osr_assert_true(lines(mods) > 0, hs_text(&label));

                hs_reset(&label);
                hs_add(&label, "rice '");
                hs_add(&label, hs_text(&rice));
                hs_add(&label, "': every layer it names is in the whole-tree scan");
                {
                    const char *q = mods;
                    int subset = 1;
                    while (*q != '\0') {
                        const char *qnl = strchr(q, '\n');
                        size_t qlen = qnl != NULL ? (size_t)(qnl - q) : strlen(q);
                        if (qlen > 0) {
                            HStr one;
                            hs_init(&one);
                            {
                                size_t i;
                                for (i = 0; i < qlen; i++) hs_addc(&one, q[i]);
                            }
                            if (!has_line(all, hs_text(&one))) subset = 0;
                            hs_free(&one);
                        }
                        if (qnl == NULL) break;
                        q = qnl + 1;
                    }
                    osr_assert_true(subset, hs_text(&label));
                }
                free(mods);
                hs_free(&rice);
                hs_free(&label);
            }
            if (nl == NULL) break;
            p = nl + 1;
        }
        free(listing);
        hs_free(&rices);
    }

    /* Every name the scan produced is a module of the C tier. `module has`
     * answers that, and it is the same question `osr module run` will ask
     * when the apply actually happens. */
    {
        const char *p = all;
        int bad = 0;
        while (*p != '\0') {
            const char *nl = strchr(p, '\n');
            size_t len = nl != NULL ? (size_t)(nl - p) : strlen(p);
            if (len > 0) {
                HStr one;
                hs_init(&one);
                {
                    size_t i;
                    for (i = 0; i < len; i++) hs_addc(&one, p[i]);
                }
                if (osr_sb_run_core(&sb, "module", "has", hs_text(&one),
                                    (const char *)NULL) != 0) bad++;
                hs_free(&one);
            }
            if (nl == NULL) break;
            p = nl + 1;
        }
        osr_assert_true(bad == 0, "every name in the scan is a module");
    }
    free(all);

    osr_sb_free(&sb);
    return osr_finish();
}
