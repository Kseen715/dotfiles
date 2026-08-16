/* test/unit_c/theme_render_test.c -- lib/theme_list.c + lib/theme_render.c.
 *
 * The synthetic fixtures (fixtures/testtheme.theme.list, app.conf.tmpl,
 * gap.conf.tmpl) are the exact scenario test/unit/theme_template.sh proves
 * against the sh implementation (same theme, same template, same expected
 * output lines) -- not because the two suites are wired together, but so a
 * human comparing them can see this is the same behavior, ported.
 *
 * The last two checks additionally render a REAL production template
 * (wezterm/wezterm-theme.toml.tmpl) against a REAL theme (themes/nord),
 * matching PLAN_UNIVERSAL.md's Task 1.2 acceptance criterion: prove this
 * against real fixtures, not only synthetic ones.
 */
#include "../c_test.h"
#include "../../lib/theme_list.h"
#include "../../lib/theme_render.h"

#include <stdio.h>
#include <string.h>

static void slurp(const char *path, char *out, unsigned long out_sz) {
    FILE *fp = fopen(path, "rb");
    size_t n;
    out[0] = '\0';
    if (fp == NULL) return;
    n = fread(out, 1, out_sz - 1, fp);
    out[n] = '\0';
    fclose(fp);
}

static void assert_file_contains(const char *path, const char *needle, const char *label) {
    char buf[8192];
    slurp(path, buf, sizeof(buf));
    osr_t_true(label, strstr(buf, needle) != NULL);
}

static void assert_file_lacks(const char *path, const char *needle, const char *label) {
    char buf[8192];
    slurp(path, buf, sizeof(buf));
    osr_t_true(label, strstr(buf, needle) == NULL);
}

static void test_synthetic_theme(void) {
    osr_theme_palette palette;
    int ok;

    ok = osr_load_theme_palette("fixtures/testtheme.theme.list", "testtheme", &palette);
    osr_t_true("synthetic: theme.list parses", ok);
    osr_t_eq_str("synthetic: display meta", osr_theme_meta_get(&palette, "display"), "Test Theme");
    osr_t_eq_str("synthetic: background color", osr_theme_color_hex(&palette, "background"), "#101010");

    ok = osr_render_template("fixtures/app.conf.tmpl", &palette, "fixtures/app.conf.out");
    osr_t_true("synthetic: template renders", ok);

    assert_file_contains("fixtures/app.conf.out", "name = testtheme (Test Theme)\n",
        "{{THEME}} is the theme's own name, alongside its display field");
    assert_file_contains("fixtures/app.conf.out", "polarity = dark\n", "a meta field substitutes");
    assert_file_contains("fixtures/app.conf.out", "gtk = Test-Adwaita\n",
        "a non-color field (a toolkit name) substitutes -- a theme is not only hexes");
    assert_file_contains("fixtures/app.conf.out", "background = #101010\n", "a color role substitutes");
    assert_file_contains("fixtures/app.conf.out", "accent = #00ff00\n",
        "every color role substitutes, not just the first");
    assert_file_contains("fixtures/app.conf.out", "accent_bare = 00ff00\n",
        "every color also has an _rgb spelling with no leading hash");
    assert_file_lacks("fixtures/app.conf.out", "{{", "nothing is left unsubstituted");

    remove("fixtures/app.conf.out");
}

static void test_missing_role_degrades(void) {
    osr_theme_palette palette;
    int ok;

    osr_load_theme_palette("fixtures/testtheme.theme.list", "testtheme", &palette);
    ok = osr_render_template("fixtures/gap.conf.tmpl", &palette, "fixtures/gap.conf.out");
    osr_t_true("gap: template renders despite a missing role", ok);

    assert_file_contains("fixtures/gap.conf.out", "background = #101010\n",
        "the rest of the file still renders");
    assert_file_contains("fixtures/gap.conf.out", "{{nosuchrole}}",
        "the unknown placeholder is left visible, not a failure");

    remove("fixtures/gap.conf.out");
}

static void test_real_wezterm_template_against_nord(void) {
    osr_theme_palette palette;
    int ok;

    ok = osr_load_theme_palette("../../themes/nord/theme.list", "nord", &palette);
    osr_t_true("real: nord/theme.list parses", ok);
    osr_t_eq_str("real: nord background color", osr_theme_color_hex(&palette, "background"), "#2e3440");

    ok = osr_render_template("../../../wezterm/wezterm-theme.toml.tmpl", &palette, "fixtures/wezterm.out");
    osr_t_true("real: wezterm-theme.toml.tmpl renders against nord", ok);

    /* The real .tmpl file has CRLF line endings (checked into the repo that
     * way); the substitution must not care either way, so this checks the
     * substituted text only, not the line terminator around it. */
    assert_file_contains("fixtures/wezterm.out", "background = \"#2e3440\"",
        "real: the rendered wezterm colors carry nord's own background");
    assert_file_lacks("fixtures/wezterm.out", "{{",
        "real: nord defines every role wezterm-theme.toml.tmpl asks for");

    remove("fixtures/wezterm.out");
}

int main(void) {
    OSR_T_INIT();
    test_synthetic_theme();
    test_missing_role_degrades();
    test_real_wezterm_template_against_nord();
    return osr_t_finish();
}
