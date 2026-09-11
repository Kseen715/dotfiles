/* test/unit_c/nerdfont_test.c -- what lib/nerdfont.c must do to install a
 * Nerd Font.
 *
 * Glyphs are a shared cosmetic asset several modules want (foot, ghostty,
 * starship, wezterm, Alacritty), which is why the download-unzip-register
 * sequence lives in one place instead of being pasted per module. Cosmetic is
 * also the contract: EVERY failure warns and returns success, because a
 * missing font must never take a rice down with it. Half of what is asserted
 * here is therefore about what does NOT happen when something goes wrong.
 *
 * Hermetic: $PATH is a directory of stubs, so "does this box have unzip",
 * "does it have fontconfig" and "which families are registered" are properties
 * of the scenario; the downloader is a stub that writes bytes where the fetch
 * asked for them, and $TMPDIR and $OSR_HOME are inside the sandbox.
 *
 * ON THE SCRATCH ZIP
 *
 * The staging file is named after the pid, which is not a decision this unit
 * made about the box -- so the name is masked before comparing. Its PRESENCE
 * still matters and is asserted through the tree: a run that leaves the zip
 * behind has leaked a scratch file into $TMPDIR.
 *
 * Was test/unit/fonts_c_parity.sh, which diffed this against lib/fonts.sh.
 * See test/harness.h for why the expectations are stated here now.
 */
#include "../harness.c"

/* The zip the no-unzip scenario feeds the built-in reader. Tests run with cwd
 * test/unit_c, so this is the same fixture archive_test.c reads. */
#define NERD_ZIP_FIXTURE "fixtures/archives/plain.zip"

static OsrSandbox sb;

/* fresh -- a scenario starts with an empty home, an empty TMPDIR and no
 * opinion from fontconfig, so nothing a previous one left can be read as
 * something this one did. */
static void fresh(void) {
    osr_sb_rm(&sb, "home");
    osr_sb_rm(&sb, "tmp");
    osr_sb_mkdir(&sb, "home");
    osr_sb_mkdir(&sb, "tmp");
    osr_sb_write(&sb, "fc-list.out", "", 0644);
    osr_sb_reset(&sb);
}

/* registered -- what `fc-list` reports. One line per registered family, in
 * fontconfig's own `<path>: <family>` shape. */
static void registered(const char *listing) {
    osr_sb_write(&sb, "fc-list.out", listing, 0644);
}

/* install -- `osr fonts nerd [family]`. */
static int install(const char *family) {
    osr_sb_reset(&sb);
    return osr_sb_run_core(&sb, "fonts", "nerd", family, (const char *)NULL);
}

int main(void) {
    HStr p;
    HStr fixture;
    char cwd[1024];

    osr_sb_init(&sb);
    hs_init(&p);
    hs_init(&fixture);
    if (getcwd(cwd, sizeof(cwd)) == NULL) cwd[0] = '\0';
    hs_path(&fixture, cwd, NERD_ZIP_FIXTURE);

    /* The version is pinned so the URL is a fact of the scenario rather than
     * of whatever lib/nerdfont.c defaults to this month. */
    osr_sb_env(&sb, "OSR_NERD_FONT_VERSION", "v3.4.0");
    hs_path(&p, hs_text(&sb.root), "tmp");
    osr_sb_env(&sb, "TMPDIR", hs_text(&p));
    hs_path(&p, hs_text(&sb.root), "fc-list.out");
    osr_sb_env(&sb, "FC_LIST", hs_text(&p));
    hs_path(&p, hs_text(&sb.root), "DL_FAIL");
    osr_sb_env(&sb, "DL_FAIL", hs_text(&p));
    hs_path(&p, hs_text(&sb.root), "UNZIP_FAIL");
    osr_sb_env(&sb, "UNZIP_FAIL", hs_text(&p));
    osr_sb_env(&sb, "ZIP_SRC", "");

    /* The staging zip carries this process's pid; the mask collapses it so an
     * expectation can name the file without naming the run. */
    osr_sb_mask(&sb, "ROOT/tmp/JetBrainsMono-");
    osr_sb_mask(&sb, "ROOT/tmp/FiraCode-");

    /* curl writes a byte or two where the fetch asked for them, so the unzip
     * that follows has something to open. */
    osr_sb_stub_body(&sb, "curl",
        "printf 'curl %s\\n' \"$*\" >>\"$LOG\"\n"
        "[ -f \"$DL_FAIL\" ] && exit 22\n"
        "_out=''\n"
        "while [ $# -gt 0 ]; do [ \"$1\" = \"-o\" ] && { _out=$2; shift; }; shift; done\n"
        "[ -n \"$_out\" ] && { if [ -f \"$ZIP_SRC\" ]; then cat \"$ZIP_SRC\" >\"$_out\";"
        " else printf 'PK\\003\\004fake-zip\\n' >\"$_out\"; fi; }\n"
        "exit 0\n");
    /* unzip lays down one file, so the tree shows that the font landed rather
     * than only that unzip was called. */
    osr_sb_stub_body(&sb, "unzip",
        "printf 'unzip %s\\n' \"$*\" >>\"$LOG\"\n"
        "[ -f \"$UNZIP_FAIL\" ] && exit 9\n"
        "_d=''\n"
        "while [ $# -gt 0 ]; do [ \"$1\" = \"-d\" ] && { _d=$2; shift; }; shift; done\n"
        "[ -n \"$_d\" ] && { mkdir -p \"$_d\"; printf 'glyphs\\n' >\"$_d/FontFile.ttf\"; }\n"
        "exit 0\n");
    osr_sb_stub_body(&sb, "fc-cache",
        "printf 'fc-cache %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
    osr_sb_stub_body(&sb, "fc-list",
        "printf 'fc-list\\n' >>\"$LOG\"\n"
        "[ -f \"$FC_LIST\" ] && cat \"$FC_LIST\"\nexit 0\n");

    /* ================================================================
     * 1. The happy path
     * ================================================================ */
    fresh();
    install(NULL);
    osr_assert_log_is(&sb,
        "sudo -u tester fc-list\n"
        "fc-list\n"
        "sudo -u tester mkdir -p ROOT/home/.local/share/fonts\n"
        /* A HEAD request first, for the total the progress line counts
         * against; it is not escalated because it writes nothing. */
        "curl -fsSLI --max-time 20 https://github.com/ryanoasis/nerd-fonts/"
        "releases/download/v3.4.0/JetBrainsMono.zip\n"
        "curl -fsSL -o ROOT/tmp/JetBrainsMono-X https://github.com/ryanoasis/"
        "nerd-fonts/releases/download/v3.4.0/JetBrainsMono.zip\n"
        "sudo -u tester unzip -q -o ROOT/tmp/JetBrainsMono-X -d "
        "ROOT/home/.local/share/fonts\n"
        "unzip -q -o ROOT/tmp/JetBrainsMono-X -d ROOT/home/.local/share/fonts\n"
        "sudo -u tester fc-cache -f ROOT/home/.local/share/fonts\n"
        "fc-cache -f ROOT/home/.local/share/fonts\n",
        "probe fontconfig, download the release, unzip into the user's font "
        "directory, refresh the cache");
    osr_assert_tree_is(&sb, "home/.local/share/fonts",
        "home/.local/share/fonts\n"
        "home/.local/share/fonts/FontFile.ttf\n",
        "the font lands in the user's own font directory, not a system one");
    osr_assert_tree_is(&sb, "tmp", "tmp\n",
        "the staging zip is cleaned up rather than left in TMPDIR");

    /* JetBrainsMono is the default because it is what the rices ask for, but
     * the family is an argument and it reaches both the URL and the probe. */
    fresh();
    install("FiraCode");
    osr_assert_log(&sb,
        "https://github.com/ryanoasis/nerd-fonts/releases/download/v3.4.0/FiraCode.zip",
        "a named family is fetched from that family's release asset");

    /* Nothing here escalates to root. The download runs unescalated into a
     * scratch file, and everything that touches the user's home -- the unzip
     * and the cache refresh -- runs as OSR_USER, because a font is user-space
     * (SS8) and fontconfig's cache is per-user. A file unpacked as root under
     * ~/.local would be one the user cannot later replace. */
    osr_refute_log(&sb, "sudo unzip",
        "the unpack never runs as root: it writes into the user's home (SS8)");
    osr_refute_log(&sb, "sudo fc-cache",
        "the cache refresh never runs as root: the cache is the user's");

    /* ================================================================
     * 2. Already registered (SS2)
     *
     * The probe is the family name and then "Nerd" later ON THE SAME LINE,
     * both case-insensitive. Same line matters: fc-list prints one font per
     * line, so a match across two lines is two different fonts.
     * ================================================================ */
    fresh();
    registered("/usr/share/fonts/JetBrainsMonoNerdFont-Regular.ttf: "
               "JetBrainsMono Nerd Font\n");
    install(NULL);
    osr_assert_log_is(&sb,
        "sudo -u tester fc-list\n"
        "fc-list\n",
        "a registered family is probed and then nothing else happens (SS2)");
    osr_assert_out(&sb, "JetBrainsMono Nerd Font already installed - skipping",
        "the skip names the family it recognised");

    fresh();
    registered("/f.ttf: jetbrainsmono nerd font mono\n");
    install(NULL);
    osr_refute_log(&sb, "curl", "the probe is case-insensitive on both halves");

    fresh();
    registered("/f.ttf: JetBrainsMono\n/g.ttf: Nerd Something Else\n");
    install(NULL);
    osr_assert_log(&sb, "curl",
        "the family and 'Nerd' on DIFFERENT lines are two fonts, not a match");

    fresh();
    registered("/f.ttf: DejaVu Sans\n");
    install(NULL);
    osr_assert_log(&sb, "curl", "an unrelated family is not a match");

    /* ================================================================
     * 3. The failure paths -- all best-effort
     *
     * A font is cosmetic, so every one of these warns and returns success. A
     * rice that cannot reach GitHub still installs; it just looks worse.
     * ================================================================ */
    fresh();
    osr_sb_write(&sb, "DL_FAIL", "", 0644);
    osr_assert_rc(install(NULL), 0, "a failed download is not fatal");
    osr_assert_err(&sb, "failed to download JetBrainsMono Nerd Font",
        "a failed download warns, and names the URL it could not reach");
    osr_refute_log(&sb, "unzip",
        "nothing is unzipped when there is nothing downloaded");
    osr_assert_tree_is(&sb, "tmp", "tmp\n",
        "a failed download leaves no half-written zip behind");
    osr_sb_rm(&sb, "DL_FAIL");

    fresh();
    osr_sb_write(&sb, "UNZIP_FAIL", "", 0644);
    osr_assert_rc(install(NULL), 0, "a failed unzip is not fatal");
    osr_assert_err(&sb, "failed to unzip JetBrainsMono Nerd Font",
        "a failed unzip warns rather than aborting the rice");
    osr_refute_log(&sb, "fc-cache",
        "the font cache is not refreshed when nothing was installed");
    osr_assert_tree_is(&sb, "tmp", "tmp\n",
        "a failed unzip still cleans up its staging file");
    osr_sb_rm(&sb, "UNZIP_FAIL");

    /* No unzip on the box -- Windows XP, or a minimal POSIX image. The
     * download still happens and the font still lands, because lib/archive.c
     * carries a zip reader; the system tool is a preference, not a
     * requirement. The stub curl serves a real zip here so the in-process
     * reader has something it can actually open. */
    fresh();
    osr_sb_rm(&sb, "bin/unzip");
    osr_sb_env(&sb, "ZIP_SRC", hs_text(&fixture));
    osr_assert_rc(install(NULL), 0, "a box without unzip is not fatal");
    osr_refute_log(&sb, "unzip",
        "no unzip is called -- there is none, and none is needed");
    osr_assert_file(&sb, "home/.local/share/fonts/p/hello.txt", "hello",
        "the zip is unpacked by the built-in reader instead");
    osr_assert_tree_is(&sb, "tmp", "tmp\n",
        "and the staging zip is still cleaned up");
    osr_sb_env(&sb, "ZIP_SRC", "");
    osr_sb_stub_body(&sb, "unzip",
        "printf 'unzip %s\\n' \"$*\" >>\"$LOG\"\n"
        "_d=''\n"
        "while [ $# -gt 0 ]; do [ \"$1\" = \"-d\" ] && { _d=$2; shift; }; shift; done\n"
        "[ -n \"$_d\" ] && { mkdir -p \"$_d\"; printf 'glyphs\\n' >\"$_d/FontFile.ttf\"; }\n"
        "exit 0\n");

    /* No fontconfig at all: the probe cannot run, so the font is installed
     * unconditionally and the cache is not refreshed. Installing it twice is
     * the right trade -- unzip -o is idempotent, and a box with no fontconfig
     * has no cache to invalidate. */
    fresh();
    osr_sb_rm(&sb, "bin/fc-list");
    osr_sb_rm(&sb, "bin/fc-cache");
    osr_assert_rc(install(NULL), 0, "a box without fontconfig is not fatal");
    osr_assert_log_is(&sb,
        "sudo -u tester mkdir -p ROOT/home/.local/share/fonts\n"
        /* A HEAD request first, for the total the progress line counts
         * against; it is not escalated because it writes nothing. */
        "curl -fsSLI --max-time 20 https://github.com/ryanoasis/nerd-fonts/"
        "releases/download/v3.4.0/JetBrainsMono.zip\n"
        "curl -fsSL -o ROOT/tmp/JetBrainsMono-X https://github.com/ryanoasis/"
        "nerd-fonts/releases/download/v3.4.0/JetBrainsMono.zip\n"
        "sudo -u tester unzip -q -o ROOT/tmp/JetBrainsMono-X -d "
        "ROOT/home/.local/share/fonts\n"
        "unzip -q -o ROOT/tmp/JetBrainsMono-X -d ROOT/home/.local/share/fonts\n",
        "with no fontconfig the font still lands, unprobed and uncached");
    osr_assert_tree_is(&sb, "home/.local/share/fonts",
        "home/.local/share/fonts\n"
        "home/.local/share/fonts/FontFile.ttf\n",
        "the font is on disk even where fontconfig cannot be told about it");

    /* ================================================================
     * 4. Termux
     *
     * The Android terminal reads exactly one file, ~/.termux/font.ttf, and
     * never asks fontconfig -- so an unpacked font directory is invisible
     * there and every glyph is a box until one .ttf is copied to that path and
     * the app is told to re-read it.
     * ================================================================ */
    osr_sb_stub_body(&sb, "termux-reload-settings",
        "printf 'termux-reload-settings\\n' >>\"$LOG\"\nexit 0\n");
    osr_sb_stub_body(&sb, "unzip",
        "printf 'unzip %s\\n' \"$*\" >>\"$LOG\"\n"
        "_d=''\n"
        "while [ $# -gt 0 ]; do [ \"$1\" = \"-d\" ] && { _d=$2; shift; }; shift; done\n"
        "[ -n \"$_d\" ] && { mkdir -p \"$_d\";"
        " printf 'mono\\n' >\"$_d/JetBrainsMonoNerdFontMono-Regular.ttf\";"
        " printf 'propo\\n' >\"$_d/JetBrainsMonoNerdFont-Regular.ttf\"; }\n"
        "exit 0\n");
    fresh();
    osr_sb_env(&sb, "TERMUX_VERSION", "0.118.0");
    osr_assert_rc(install(NULL), 0, "the Termux path is best-effort too");
    osr_assert_file(&sb, "home/.termux/font.ttf", "mono\n",
        "the Mono cut is what lands at ~/.termux/font.ttf: the terminal is "
        "monospaced and that variant's advance widths are the cell");
    osr_assert_log(&sb, "termux-reload-settings",
        "the running terminal is told to re-read it, or it keeps the old face "
        "until the app restarts");

    /* The fonts being unpacked is not the same fact as the terminal pointing
     * at one, so the already-installed skip still wires font.ttf. */
    osr_sb_reset(&sb);
    (void)install(NULL);
    osr_assert_out(&sb, "already installed - skipping",
        "the second run skips the download");
    osr_refute_log(&sb, "termux-reload-settings",
        "an unchanged font.ttf is not rewritten, so the terminal is not "
        "reloaded (and its scrollback not cleared) for nothing");

    osr_sb_rm(&sb, "home/.termux");
    osr_sb_reset(&sb);
    (void)install(NULL);
    osr_assert_file(&sb, "home/.termux/font.ttf", "mono\n",
        "and a font.ttf deleted by hand is put back on the next run");
    osr_sb_env(&sb, "TERMUX_VERSION", "");

    hs_free(&p);
    hs_free(&fixture);
    osr_sb_free(&sb);
    return osr_finish();
}
