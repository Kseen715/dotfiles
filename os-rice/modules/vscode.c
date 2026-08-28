/* modules/vscode.c -- VS Code, the distro-packaged build (Void ships `vscode`,
 * Arch `code`; the MS-branded Insiders channel is the Arch-only sibling module
 * `vscode-insiders`). Never list both in one rice.
 *
 * Deliberately NOT config-managed. VS Code has its own first-class settings sync
 * (Settings Sync, signed into a GitHub/Microsoft account) plus a profiles system,
 * and both write the same settings.json that os-rice would own. Two managers on
 * one file means whichever ran last wins and the other silently loses edits — so
 * this module installs the editor and stops there.
 *
 * That includes the theme: pick it in VS Code (or let Settings Sync carry it),
 * not here. The rice ships no VS Code palette on purpose.
 *
 * Port of modules/vscode.sh, kept as the reference at
 * test/ref/vscode_sh_ref.sh. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_vscode(void) {
    static const char *const pkgs[] = { "vscode", NULL };
    return osr_pkg_install_step("Installing VS Code", pkgs);
}
