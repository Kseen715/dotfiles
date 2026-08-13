/* modules.h -- the finite set of Windows modules this C tier can actually
 * run: fastfetch, oh-my-posh, pwsh, wezterm. Ported once from the
 * now-retired windows-rice/modules folder (*.ps1) -- see PLAN_UNIVERSAL.md
 * decision 8 -- and now the only Windows rice implementation this repo
 * ships. See modules.c's header comment for the full ps1-to-C mapping.
 *
 * This is NOT an attempt at the ~70 Linux os-rice/modules folder (*.sh) -- those
 * assume a Linux desktop (X11/Wayland, GTK, systemd units) that has no
 * Windows equivalent. PLAN_UNIVERSAL.md's own "Not Doing" section already
 * rules that out; this file covers exactly the modules windows-rice
 * itself had already decided were the realistic Windows set.
 *
 * C89.
 */
#ifndef OSR_MODULES_H
#define OSR_MODULES_H

/* osr_known_module -- 1 if `name` is one of the modules this file can
 * actually run (fastfetch, oh-my-posh, pwsh, wezterm), else 0.
 */
int osr_known_module(const char *name);

/* osr_run_module -- full install: package + font (where needed) + config
 * (dotfiles-owned and/or theme-rendered). repo_root is the dotfiles
 * checkout root (the parent of os-rice/ -- where wezterm/, fastfetch/,
 * PowerShell7-profile/ live). Only call this after osr_known_module(name)
 * returned 1. A failure is reported (osr_warn) and returns 0; it never
 * exits the process -- one broken module must not abort the whole rice
 * install, same as install.sh's own run_module.
 */
int osr_run_module(const char *repo_root, const char *name, const char *theme);

/* osr_apply_module_theme -- re-render + reinstall just the theme-owned
 * config layer for `name` (no package install, no font install) -- the
 * theme-only path (install.exe --theme-only), mirrors osr_apply_theme's
 * "neutralize every install verb" behavior. pwsh carries no theme layer
 * (its config is dotfiles-owned, not theme-owned) and is a silent no-op
 * success here, matching install.sh's OSR_THEME_MARKERS grep excluding it.
 */
int osr_apply_module_theme(const char *repo_root, const char *name, const char *theme);

#endif /* OSR_MODULES_H */
