/* modules/vscode-insiders.c -- VS Code Insiders (AUR) + coding fonts. ONE copy,
 * POSIX (was .../apps/vscode-insiders.sh). Maps vscode-insiders ->
 * aur:visual-studio-code-insiders-bin.
 *
 * Was modules/vscode-insiders.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

int osrm_vscode_insiders(void) {
    static const char *const pkgs[] = { "vscode-insiders", NULL };
    static const char *const fonts[] = {
        "ttf-cascadia-code-nerd", "ttf-cascadia-mono-nerd", "ttf-iosevkaterm-nerd", NULL
    };
    int ok;

    ok = osr_pkg_install_step("Installing VS Code Insiders (AUR)", pkgs);
    return osr_pkg_install_step("Installing coding fonts", fonts) && ok;
}
