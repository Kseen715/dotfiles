/* test/unit_c/winpkg_test.c -- lib/winpkg.c's windows.map lookup.
 * Platform-independent: osr_winpkg_lookup is plain fopen/fgets, exercised
 * here against the real os-rice/windows.map fixture, not a synthetic
 * copy, so drift between the two never goes unnoticed.
 */
#include "../c_test.h"
#include "../../lib/winpkg.h"

#define MAP_PATH "../../windows.map"

static void test_known_entries(void) {
    osr_winpkg_spec spec;

    osr_t_true("lookup: wezterm found", osr_winpkg_lookup(MAP_PATH, "wezterm", &spec));
    osr_t_true("lookup: wezterm has scoop", spec.has_scoop);
    osr_t_eq_str("lookup: wezterm scoop id", spec.scoop, "wezterm");
    osr_t_true("lookup: wezterm has choco", spec.has_choco);
    osr_t_eq_str("lookup: wezterm choco id", spec.choco, "wezterm");
    osr_t_true("lookup: wezterm has winget", spec.has_winget);
    osr_t_eq_str("lookup: wezterm winget id", spec.winget, "wez.wezterm");

    osr_t_true("lookup: pwsh found", osr_winpkg_lookup(MAP_PATH, "pwsh", &spec));
    osr_t_eq_str("lookup: pwsh scoop id", spec.scoop, "pwsh");
    osr_t_eq_str("lookup: pwsh choco id", spec.choco, "powershell-core");
    osr_t_eq_str("lookup: pwsh winget id", spec.winget, "Microsoft.PowerShell");
}

static void test_missing_entry(void) {
    osr_winpkg_spec spec;
    int found = osr_winpkg_lookup(MAP_PATH, "definitely-not-a-real-package", &spec);
    osr_t_true("lookup: unknown name returns not-found", !found);
    osr_t_true("lookup: unknown name leaves spec empty", !spec.has_scoop && !spec.has_choco && !spec.has_winget);
}

static void test_missing_file(void) {
    osr_winpkg_spec spec;
    int found = osr_winpkg_lookup("no/such/file.map", "wezterm", &spec);
    osr_t_true("lookup: missing map file returns not-found", !found);
}

int main(void) {
    OSR_T_INIT();
    test_known_entries();
    test_missing_entry();
    test_missing_file();
    return osr_t_finish();
}
