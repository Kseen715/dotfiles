/* modules/hyprpicker.c -- hyprpicker color picker. ONE copy, POSIX
 * (was .../modules/hyprpicker.sh). Native on Arch, no config.
 *
 * Port of modules/hyprpicker.sh, kept as the reference at
 * test/ref/hyprpicker_sh_ref.sh. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_hyprpicker(void) {
    static const char *const pkgs[] = { "hyprpicker", NULL };
    return osr_pkg_install_step("Installing hyprpicker", pkgs);
}
