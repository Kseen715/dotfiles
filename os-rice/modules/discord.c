/* modules/discord.c -- Discord. ONE copy, POSIX (was .../apps/discord.sh).
 *
 * Port of modules/discord.sh, kept as the reference at
 * test/ref/discord_sh_ref.sh. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_discord(void) {
    static const char *const pkgs[] = { "discord", NULL };
    return osr_pkg_install_step("Installing Discord", pkgs);
}
