/* modules/discord.c -- Discord. ONE copy, POSIX (was .../apps/discord.sh).
 *
 * Was modules/discord.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_discord(void) {
    static const char *const pkgs[] = { "discord", NULL };
    return osr_pkg_install_step("Installing Discord", pkgs);
}
