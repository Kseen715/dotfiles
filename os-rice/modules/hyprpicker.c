/* modules/hyprpicker.c -- hyprpicker color picker. ONE copy, POSIX
 * (was .../modules/hyprpicker.sh). Native on Arch, no config.
 *
 * Was modules/hyprpicker.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_hyprpicker(void) {
    static const char *const pkgs[] = { "hyprpicker", NULL };
    return osr_pkg_install_step("Installing hyprpicker", pkgs);
}
