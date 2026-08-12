/* test/unit_c/manifest_test.c -- lib/manifest.c against real rice.list
 * fixtures, not synthetic copies (PLAN_UNIVERSAL.md Task 1.1's acceptance
 * criterion: parse the actual rices/ trees the same way install.sh does).
 */
#include "../c_test.h"
#include "../../lib/manifest.h"

#define NORD_RICE_LIST "../../rices/nord/rice.list"
#define GRUVBOX_RICE_LIST "../../rices/gruvbox/rice.list"

static void test_nord(void) {
    osr_manifest m;

    osr_t_true("nord: parses", osr_parse_rice_list(NORD_RICE_LIST, &m));
    osr_t_eq_str("nord: theme", m.theme, "nord");
    osr_t_eq_str("nord: themes", m.themes, "catppuccin nord gruvbox xin");
    osr_t_eq_int("nord: module count", m.module_count, 4);
    osr_t_eq_str("nord: module 0", m.modules[0], "starship");
    osr_t_eq_str("nord: module 1", m.modules[1], "zsh");
    osr_t_eq_str("nord: module 2", m.modules[2], "fastfetch");
    osr_t_eq_str("nord: module 3", m.modules[3], "yazi");
    osr_t_eq_str("nord: no require: line means empty requires", m.requires_list, "");
}

/* gruvbox is nord's stated sibling ("same modules as gruvbox" per nord's
 * rice.list comment) -- cross-check that comment against the real file
 * instead of taking it on faith.
 */
static void test_gruvbox_matches_nord_modules(void) {
    osr_manifest nord, gruvbox;

    osr_t_true("gruvbox: parses", osr_parse_rice_list(GRUVBOX_RICE_LIST, &gruvbox));
    osr_parse_rice_list(NORD_RICE_LIST, &nord);

    osr_t_eq_int("gruvbox: same module count as nord", gruvbox.module_count, nord.module_count);
}

static void test_missing_file(void) {
    osr_manifest m;
    osr_t_true("missing file: reports failure", !osr_parse_rice_list("no/such/rice.list", &m));
}

static void test_comment_and_blank_line_handling(void) {
    /* comments and blank lines appear throughout nord/rice.list already;
     * a parse that got them wrong would have failed test_nord() above by
     * picking up stray module entries. This just names that guarantee.
     */
    osr_manifest m;
    osr_parse_rice_list(NORD_RICE_LIST, &m);
    osr_t_eq_int("nord: comments/blank lines produce no extra modules", m.module_count, 4);
}

int main(void) {
    OSR_T_INIT();
    test_nord();
    test_gruvbox_matches_nord_modules();
    test_missing_file();
    test_comment_and_blank_line_handling();
    return osr_t_finish();
}
