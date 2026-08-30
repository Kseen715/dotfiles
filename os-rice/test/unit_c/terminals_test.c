/* test/unit_c/terminals_test.c -- the terminal emulators and the other apps
 * whose config is a base plus a theme-owned palette layer.
 *
 * alacritty, foot, ghostty, wezterm and serie all have the same shape, which
 * is why they share a file. Each installs:
 *
 *   A BASE, owned by the dotfiles. Font, keybindings, behaviour. Rewritten on
 *   every run, and it carries NO colours at all.
 *
 *   A PALETTE LAYER, owned by the theme, in a separate file the base includes.
 *   The rice's version wins; the dotfiles ship a default so an app is never
 *   left unthemed.
 *
 * The split is the point (SS5, SS6). Colours in the base would mean a theme
 * switch had to rewrite a file full of the user's font and keybinding choices,
 * and every switch would be a chance to lose them. So the negative assertions
 * below -- "the base carries no palette", "no opacity" -- are load-bearing:
 * they are what keeps a theme switch to one small file per app.
 *
 * These run against the REAL dotfiles tree, because what is asserted is partly
 * that those files say what they are supposed to say. A base that stopped
 * including the palette layer would leave every terminal unthemed with nothing
 * failing anywhere else.
 *
 * Hermetic: $PATH is stubs, so no package manager runs, no font is downloaded
 * and every version probe answers what the scenario says.
 *
 * Replaces test/unit/alacritty_module.sh, foot_module.sh, ghostty_module.sh,
 * wezterm_module.sh and serie_module.sh. See test/harness.h.
 */
#include "../harness.c"

static OsrSandbox sb;

static char *read_rel(const char *rel) {
    HStr path;
    char *got;
    hs_init(&path);
    hs_path(&path, hs_text(&sb.root), rel);
    got = h_slurp(hs_text(&path));
    hs_free(&path);
    return got;
}
static void holds(const char *rel, const char *needle, const char *label) {
    char *got = read_rel(rel);
    osr_assert_true(strstr(got, needle) != NULL, label);
    free(got);
}
static void lacks(const char *rel, const char *needle, const char *label) {
    char *got = read_rel(rel);
    osr_assert_true(strstr(got, needle) == NULL, label);
    free(got);
}

/* no_directive -- no LINE of the file begins with `prefix`.
 *
 * A plain substring search is the wrong question for "does this config set
 * colours": these files document what they deliberately do not set, so
 * `[colors]` and `opacity` both appear -- in comments explaining their
 * absence. What matters is whether anything is set, and in every format here
 * a setting starts its line. */
static void no_directive(const char *rel, const char *prefix, const char *label) {
    char *got = read_rel(rel);
    const char *p = got;
    size_t n = strlen(prefix);
    int hit = 0;

    while (*p != '\0' && !hit) {
        if (strncmp(p, prefix, n) == 0) hit = 1;
        while (*p != '\0' && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    osr_assert_true(!hit, label);
    free(got);
}

/* rice_ships -- the theme's own version of an app's palette layer. */
static void rice_ships(const char *rel, const char *body) {
    HStr full;
    hs_init(&full);
    hs_add(&full, "theme/config/");
    hs_add(&full, rel);
    osr_sb_write(&sb, hs_text(&full), body, 0644);
    hs_free(&full);
}

/* fresh -- an empty home and an empty theme, so each scenario states its own. */
static void fresh(void) {
    osr_sb_rm(&sb, "home");
    osr_sb_rm(&sb, "theme");
    osr_sb_mkdir(&sb, "home");
    osr_sb_mkdir(&sb, "theme");
    osr_sb_reset(&sb);
}

static void run_module(const char *name) {
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "module", "run", name, (const char *)NULL);
}

/* ver_stub -- an installed app that reports a version. */
static void ver_stub(const char *name, const char *body) {
    osr_sb_stub_body(&sb, name, body);
}

int main(void) {
    HStr p;

    osr_sb_init(&sb);
    hs_init(&p);

    hs_path(&p, hs_text(&sb.osr_root), "..");
    osr_sb_env(&sb, "OSR_DOTFILES", hs_text(&p));
    hs_path(&p, hs_text(&sb.root), "theme");
    osr_sb_env(&sb, "OSR_THEME_DIR", hs_text(&p));
    osr_sb_env(&sb, "OSR_THEME", "demo");
    osr_sb_env(&sb, "OSR_PKG", "apt");
    osr_sb_env(&sb, "OSR_INIT", "systemd");

    osr_sb_stub_body(&sb, "dpkg", "exit 1\n");
    osr_sb_stub_body(&sb, "apt-mark", "exit 0\n");
    osr_sb_stub_body(&sb, "apt-get",
        "case \"$1\" in install) printf 'apt-get %s\\n' \"$*\" >>\"$LOG\" ;; esac\n"
        "exit 0\n");
    /* The Nerd Font is already registered, so no download is attempted -- and
     * curl logs loudly so an attempt would be visible. */
    osr_sb_stub_body(&sb, "fc-list",
        "printf '/f.ttf: JetBrainsMono Nerd Font\\n'\n");
    osr_sb_stub_body(&sb, "fc-cache", "exit 0\n");
    osr_sb_stub_body(&sb, "unzip", "exit 0\n");
    osr_sb_stub_body(&sb, "curl",
        "printf 'curl %s\\n' \"$*\" >>\"$LOG\"\nexit 1\n");

    /* ================================================================
     * 1. Alacritty
     * ================================================================ */
    ver_stub("alacritty", "printf 'alacritty 0.15.1 (1234abc)\\n'\n");
    fresh();
    rice_ships("alacritty/alacritty-theme.toml",
        "# RICE-PALETTE-MARKER\n[colors.primary]\nbackground = \"#123456\"\n");
    run_module("alacritty");

    osr_assert_log(&sb, "alacritty",
        "alacritty: the package is installed");
    osr_assert_log(&sb, "unzip",
        "alacritty: and the font dependencies alongside it -- a terminal with "
        "no Nerd Font renders every prompt glyph as a box");

    holds("home/.config/alacritty/alacritty.toml", "JetBrainsMono Nerd Font",
        "alacritty: the dotfiles-owned base is installed");
    holds("home/.config/alacritty/alacritty.toml", "[general]",
        "alacritty: 0.14+ keeps the [general] section");
    holds("home/.config/alacritty/alacritty.toml",
        "import = [\"~/.config/alacritty/alacritty-theme.toml\"]",
        "alacritty: and the base INCLUDES the palette layer -- without this "
        "line every theme lands in a file nothing reads");
    holds("home/.config/alacritty/alacritty.toml", "TERM = \"xterm-256color\"",
        "alacritty: TERM is set to something every remote host has a terminfo "
        "entry for");
    holds("home/.config/alacritty/alacritty.toml", "action = \"ClearHistory\"",
        "alacritty: the base ships its keybinding layer");
    holds("home/.config/alacritty/alacritty.toml", "key = \"ArrowLeft\"",
        "alacritty: bindings use the 0.13+ W3C key names");
    no_directive("home/.config/alacritty/alacritty.toml", "key = \"Left\"",
        "alacritty: and not the pre-0.13 spellings, which 0.13+ rejects");

    no_directive("home/.config/alacritty/alacritty.toml", "[colors",
        "alacritty: the base carries NO palette -- colours are theme-owned, so "
        "a theme switch never rewrites the file holding the user's font and "
        "keybindings (SS5)");
    no_directive("home/.config/alacritty/alacritty.toml", "opacity",
        "alacritty: nor transparency, which is a theme's decision too (SS6)");

    holds("home/.config/alacritty/alacritty-theme.toml", "RICE-PALETTE-MARKER",
        "alacritty: the rice's palette wins over the dotfiles default");
    osr_refute_log(&sb, "curl",
        "alacritty: a Nerd Font already registered is not downloaded again (SS2)");

    /* No rice palette: the dotfiles default lands instead, so the terminal is
     * never left with an empty theme file. */
    fresh();
    run_module("alacritty");
    holds("home/.config/alacritty/alacritty-theme.toml", "[colors.normal]",
        "alacritty: with no rice palette the dotfiles default is used");
    holds("home/.config/alacritty/alacritty-theme.toml", "opacity",
        "alacritty: and the theme layer is what owns window.opacity");

    /* ================================================================
     * 2. foot -- the same shape, plus a version-adapted section name
     * ================================================================ */
    ver_stub("foot", "printf 'foot version: 1.26.0\\n'\n");
    fresh();
    rice_ships("foot/foot-colors.ini",
        "# RICE-PALETTE-MARKER\n[colors-dark]\nbackground=123456\n");
    run_module("foot");
    osr_assert_log(&sb, "foot", "foot: the package is installed");
    holds("home/.config/foot/foot.ini", "JetBrainsMono",
        "foot: the dotfiles-owned base is installed");
    holds("home/.config/foot/foot-colors.ini", "RICE-PALETTE-MARKER",
        "foot: the rice's palette wins");
    holds("home/.config/foot/foot-colors.ini", "[colors-dark]",
        "foot: 1.26 understands the light/dark sections, so they are kept");
    osr_refute_log(&sb, "curl",
        "foot: the font is not re-downloaded (SS2)");

    /* An older foot rejects the file outright if it carries a section it does
     * not know -- and a terminal that will not start is one the user cannot
     * fix from inside. */
    ver_stub("foot", "printf 'foot version: 1.20.2\\n'\n");
    fresh();
    run_module("foot");
    holds("home/.config/foot/foot-colors.ini", "regular0",
        "foot: with no rice palette the dotfiles default is used");
    holds("home/.config/foot/foot-colors.ini", "[colors]",
        "foot: and on 1.20 the section is downgraded to the name it knows");
    no_directive("home/.config/foot/foot-colors.ini", "[colors-dark]",
        "foot: with no section left that an old foot would refuse to parse");

    /* ================================================================
     * 3. ghostty
     * ================================================================ */
    ver_stub("ghostty", "[ \"$1\" = \"+version\" ] && printf 'Version: 1.2.0\\n'\nexit 0\n");
    fresh();
    rice_ships("ghostty/ghostty-theme",
        "# RICE-PALETTE-MARKER\nbackground = #123456\n");
    run_module("ghostty");
    osr_assert_log(&sb, "unzip",
        "ghostty: the font dependencies are installed -- ghostty itself is a "
        "source: row, not a package");
    holds("home/.config/ghostty/config", "JetBrainsMono",
        "ghostty: the dotfiles-owned base is installed");
    holds("home/.config/ghostty/config", "background-opacity = 0.85",
        "ghostty: the base sets its transparency");
    holds("home/.config/ghostty/config", "ssh-terminfo",
        "ghostty: ssh terminfo integration is on -- ghostty's own terminfo is "
        "on no remote host, and without this every ssh session is broken");
    holds("home/.config/ghostty/config", "clipboard-write = allow",
        "ghostty: OSC 52 writes are allowed, so yanking on a remote host "
        "reaches the local clipboard");
    holds("home/.config/ghostty/config", "config-file = ?ghostty-theme",
        "ghostty: and the base includes the palette layer, optionally -- the "
        "`?` is what keeps a missing theme file from being a startup error");
    holds("home/.config/ghostty/ghostty-theme", "RICE-PALETTE-MARKER",
        "ghostty: the rice's palette wins");

    fresh();
    run_module("ghostty");
    holds("home/.config/ghostty/ghostty-theme", "palette = 0=",
        "ghostty: with no rice palette the dotfiles default is used");
    osr_refute_log(&sb, "curl", "ghostty: the font is not re-downloaded (SS2)");

    /* ================================================================
     * 4. wezterm
     *
     * Already installed, so the source: row's own probe short-circuits: what
     * is under test is the config layering, not a Rust build.
     * ================================================================ */
    ver_stub("wezterm", "printf 'wezterm 20240203\\n'\nexit 0\n");
    fresh();
    rice_ships("wezterm/wezterm-theme.toml",
        "# RICE-PALETTE-MARKER\n[colors]\nbackground = \"#123456\"\n");
    run_module("wezterm");
    osr_assert_log(&sb, "unzip",
        "wezterm: the font dependencies are installed -- wezterm is a source: "
        "row too");
    holds("home/.wezterm.lua", "wezterm.config_builder",
        "wezterm: the dotfiles-owned base lands in the home directory, where "
        "wezterm looks for it");
    holds("home/.wezterm.lua", "colors/osr-rice.toml",
        "wezterm: and it selects the palette layer");
    holds("home/.config/wezterm/colors/osr-rice.toml", "RICE-PALETTE-MARKER",
        "wezterm: the rice's palette wins");

    fresh();
    run_module("wezterm");
    holds("home/.config/wezterm/colors/osr-rice.toml", "name = \"osr-rice\"",
        "wezterm: the dotfiles default carries the scheme NAME the base "
        "selects -- a palette under a different name is one wezterm ignores");

    /* ================================================================
     * 5. serie
     *
     * On pacman, because on apt/dnf/xbps `serie` is a cargo: row and the
     * module installs a Rust toolchain first -- which is a different test.
     * What is under test here is the theme layer.
     * ================================================================ */
    osr_sb_env(&sb, "OSR_PKG", "pacman");
    osr_sb_env(&sb, "OSR_DISTRO", "arch");
    osr_sb_env(&sb, "OSR_ID_LIKE", "");
    osr_sb_stub_body(&sb, "pacman",
        "case \"$1\" in -Q*) exit 1 ;; esac\n"
        "printf 'pacman %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
    fresh();
    rice_ships("serie/config.toml",
        "# RICE-THEME-MARKER\n[color]\nfg = \"#123456\"\n");
    run_module("serie");
    osr_assert_log(&sb, "serie", "serie: the package is installed");
    holds("home/.config/serie/config.toml", "RICE-THEME-MARKER",
        "serie: the rice's theme wins over the dotfiles default");

    fresh();
    run_module("serie");
    holds("home/.config/serie/config.toml", "[color]",
        "serie: with no rice theme the dotfiles default is used");
    holds("home/.config/serie/config.toml", "branches",
        "serie: and the graph's branch colours are themed too, not just the "
        "text -- the graph is most of what serie draws");

    hs_free(&p);
    osr_sb_free(&sb);
    return osr_finish();
}
