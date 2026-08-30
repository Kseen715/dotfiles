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
    osr_assert_true(lines(verbs) > 10, "the verb list is the real one");

    /* The list is derived from the functions that ask osr_theme_only() before
     * acting, so what these assert is that each KIND of mutation is covered.
     * A verb that forgot the check would be absent here and would also run
     * for real during a theme apply -- the same defect, seen twice. */
    osr_assert_true(has_line(verbs, "osr_pkg_install"),
                    "installing a package is neutralized");
    osr_assert_true(has_line(verbs, "osr_pkg_remove"),
                    "and removing one");
    osr_assert_true(has_line(verbs, "osr_pkg_refresh"),
                    "and refreshing the index, which is a network round trip");
    osr_assert_true(has_line(verbs, "osr_service_enable"),
                    "enabling a service is neutralized");
    osr_assert_true(has_line(verbs, "osr_service_disable"),
                    "and disabling one");
    osr_assert_true(has_line(verbs, "osr_fetch_download"),
                    "downloading a file is neutralized -- a theme switch that "
                    "reached the network would not be a hotkey");
    osr_assert_true(has_line(verbs, "osr_build_run"),
                    "and running a source builder, which is the slowest thing "
                    "in the tree");
    osr_assert_true(has_line(verbs, "osr_git_repo"),
                    "cloning a repository is neutralized");
    osr_assert_true(has_line(verbs, "osr_install_nerd_font"),
                    "and installing a font");
    osr_assert_true(has_line(verbs, "osr_run_root"),
                    "and the escalation itself -- which is the backstop: a "
                    "mutation that slipped past every verb above still cannot "
                    "become root during an apply");

    /* What must NOT be in it: the queries a module branches on. Neutralizing
     * one of those does not prevent a mutation, it makes the module take the
     * wrong branch and write the wrong config. */
    osr_assert_true(!has_line(verbs, "osr_pkg_installed"),
                    "`is it installed` is a QUERY and is not neutralized -- a "
                    "stubbed probe makes a module take the wrong branch, which "
                    "is worse than the mutation it was meant to prevent");
    osr_assert_true(!has_line(verbs, "osr_pkgmap_resolve"),
                    "nor is name resolution: stubbing it would resolve every "
                    "package name to nothing");
    osr_assert_true(!has_line(verbs, "osr_service_resolve"),
                    "nor is the servicemap lookup");
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

    /* ================================================================
     * 4. The apply itself, end to end
     *
     * `osr apply theme <name>` is the hotkey path: swap the look, touch
     * nothing else. Three promises, and each one is a thing a user would
     * notice being broken within a minute:
     *
     *   IT INSTALLS NOTHING. Every mutating verb is neutralised for the rest
     *   of the process, so an apply cannot reach the network or a package
     *   manager -- which is what makes it fast enough to bind to a key.
     *
     *   IT IS A PURE FUNCTION OF THE THEME. A -> B -> A returns the identical
     *   file. A composed config that accumulated would drift with every
     *   switch, and the drift would only show up much later.
     *
     *   IT IS IDEMPOTENT. Applying the same theme twice changes nothing on
     *   disk (SS2).
     *
     * Driven through `osr apply theme` directly and NEVER through
     * `install.sh --theme-only`: the runner resolves $OSR_HOME from passwd, so
     * running that here would apply a theme to the home of whoever runs the
     * suite. This entry point takes $OSR_HOME from the environment, which is
     * exactly why osr_apply_theme lives in a lib rather than inline in the
     * runner.
     * ================================================================ */
    {
        HStr dots;
        char *before;
        char *after;
        char *nord_first;

        hs_init(&dots);
        hs_path(&dots, hs_text(&sb.osr_root), "..");
        osr_sb_env(&sb, "OSR_DOTFILES", hs_text(&dots));
        hs_free(&dots);
        osr_sb_env(&sb, "OSR_PKG", "apt");
        osr_sb_env(&sb, "OSR_DISTRO", "ubuntu");
        osr_sb_env(&sb, "OSR_ID_LIKE", "debian");
        osr_sb_env(&sb, "OSR_CODENAME", "noble");
        osr_sb_env(&sb, "OSR_VERSION_ID", "24.04");
        osr_sb_env(&sb, "OSR_INIT", "systemd");
        osr_sb_real(&sb, "python3");

        /* A small rice, so the apply narrows to a handful of layers. The theme
         * applied over it is deliberately a DIFFERENT name from the rice: the
         * two are separate axes (SS6), and conflating them is the bug this
         * arrangement would catch. */
        osr_sb_rm(&sb, "home");
        osr_sb_mkdir(&sb, "home/.config/osr");
        osr_sb_write(&sb, "home/.config/osr/state", "rice=catppuccin\n", 0644);

        osr_sb_reset(&sb);
        osr_assert_rc(osr_sb_run_core(&sb, "apply", "theme", "nord",
                                      (const char *)NULL), 0,
                      "apply: a theme apply succeeds");
        osr_assert_out(&sb, "applying theme 'nord'",
                       "apply: and says which theme it is applying");

        {
            HStr layer;
            char *text;
            hs_init(&layer);
            hs_path(&layer, hs_text(&sb.root),
                    "home/.config/osr/zsh/rc.d/90-theme.zsh");
            text = h_slurp(hs_text(&layer));
            osr_assert_true(strstr(text, "OSR_RICE_THEME=\"nord\"") != NULL,
                "apply: the zsh theme layer landed, and it is nord's");
            free(text);
            hs_free(&layer);
        }
        {
            HStr toml;
            char *text;
            hs_init(&toml);
            hs_path(&toml, hs_text(&sb.root), "home/.config/starship.toml");
            text = h_slurp(hs_text(&toml));
            osr_assert_true(strstr(text, "#88c0d0") != NULL,
                "apply: starship.toml was composed with nord's palette");
            nord_first = text;
            hs_free(&toml);
        }

        /* The state records the theme AND keeps the rice: they are separate
         * axes, and an apply that forgot the rice would narrow the next one
         * to every module in the tree. */
        {
            HStr st;
            char *text;
            hs_init(&st);
            hs_path(&st, hs_text(&sb.root), "home/.config/osr/state");
            text = h_slurp(hs_text(&st));
            osr_assert_true(strstr(text, "theme=nord") != NULL,
                "apply: the state records the applied theme");
            osr_assert_true(strstr(text, "rice=catppuccin") != NULL,
                "apply: and keeps the rice -- theme and rice are separate axes");
            free(text);
            hs_free(&st);
        }

        /* No package manager was ever consulted. A stray call would have
         * needed the network or a sudo ticket; this asserts on the output
         * rather than trusting that it did not. */
        {
            static const char *const forbidden[] = {
                "Installing ", "apt-get", "pacman", "xbps-install",
                "dnf install", NULL
            };
            const char *out = osr_sb_capture_both(&sb);
            int clean = 1;
            int i;
            for (i = 0; forbidden[i] != NULL; i++) {
                if (strstr(out, forbidden[i]) != NULL) clean = 0;
            }
            osr_assert_true(clean,
                "apply: no package or install step ran at all -- every mutating "
                "verb is neutralised for the rest of the process, which is what "
                "makes an apply fast enough to bind to a hotkey");
            osr_assert_true(strstr(out, "password") == NULL &&
                            strstr(out, "sudo:") == NULL,
                "apply: and nothing prompted for sudo -- a root layer with no "
                "ticket is skipped, not asked about");
        }

        /* Switching replaces, rather than accumulating. */
        osr_sb_reset(&sb);
        osr_sb_run_core(&sb, "apply", "theme", "gruvbox", (const char *)NULL);
        {
            HStr layer, toml;
            char *text;
            hs_init(&layer);
            hs_path(&layer, hs_text(&sb.root),
                    "home/.config/osr/zsh/rc.d/90-theme.zsh");
            text = h_slurp(hs_text(&layer));
            osr_assert_true(strstr(text, "OSR_RICE_THEME=\"gruvbox\"") != NULL,
                "switch: the zsh layer is now gruvbox's");
            free(text);
            hs_free(&layer);

            hs_init(&toml);
            hs_path(&toml, hs_text(&sb.root), "home/.config/starship.toml");
            text = h_slurp(hs_text(&toml));
            osr_assert_true(strstr(text, "#fabd2f") != NULL,
                "switch: starship.toml carries gruvbox's palette");
            osr_assert_true(strstr(text, "#88c0d0") == NULL,
                "switch: and NO trace of the previous theme -- a composed file "
                "that accumulated would be a TOML parse error, or worse, two "
                "palettes and a coin toss");
            free(text);
            hs_free(&toml);
        }

        /* A -> B -> A is the identical file. */
        osr_sb_reset(&sb);
        osr_sb_run_core(&sb, "apply", "theme", "nord", (const char *)NULL);
        {
            HStr toml;
            hs_init(&toml);
            hs_path(&toml, hs_text(&sb.root), "home/.config/starship.toml");
            after = h_slurp(hs_text(&toml));
            osr_assert_eq(nord_first, after,
                "switch: A -> B -> A returns the identical file -- a theme is "
                "a pure function of itself, not of the order themes were "
                "applied in");
            hs_free(&toml);
        }
        free(nord_first);

        /* SS2: the same theme twice changes nothing on disk. */
        before = after;
        osr_sb_reset(&sb);
        osr_sb_run_core(&sb, "apply", "theme", "nord", (const char *)NULL);
        {
            HStr toml;
            char *again2;
            hs_init(&toml);
            hs_path(&toml, hs_text(&sb.root), "home/.config/starship.toml");
            again2 = h_slurp(hs_text(&toml));
            osr_assert_eq(before, again2,
                "apply: re-applying the same theme is a no-op (SS2)");
            free(again2);
            hs_free(&toml);
        }
        free(before);
    }

    osr_sb_free(&sb);
    return osr_finish();
}
