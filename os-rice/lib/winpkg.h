/* lib/winpkg.h -- windows.map lookup + scoop/choco/winget dispatch.
 *
 * C port of os-rice/windows-rice/src/pkg.ps1's Get-WindowsMapSpec /
 * Install-RicePackage, reading the same os-rice/windows-rice/windows.map
 * file (format: `name = mgr:id [mgr:id ...]`, mgr in {scoop,choco,winget}).
 *
 * This is deliberately NOT a port of lib/pkg.sh: pkg.sh's problem is
 * apt/pacman/apk dispatch, a different package model than Windows' three
 * managers. windows.map / pkg.ps1 already solved the Windows side; this
 * file is that solution ported to C, not a new design.
 *
 * C89.
 */
#ifndef OSR_WINPKG_H
#define OSR_WINPKG_H

#define OSR_WINPKG_MGR_MAX 64  /* max bytes for one manager's package id */

typedef struct {
    char scoop[OSR_WINPKG_MGR_MAX];
    char choco[OSR_WINPKG_MGR_MAX];
    char winget[OSR_WINPKG_MGR_MAX];
    int has_scoop;
    int has_choco;
    int has_winget;
} osr_winpkg_spec;

/* osr_winpkg_lookup -- find `name`'s entry in the windows.map at map_path.
 * Pure parsing, no OS dependency (unit-tested against the real
 * windows-rice/windows.map fixture). Returns 1 if an entry was found (even
 * if empty), 0 if the file has no line for `name` or can't be opened.
 */
int osr_winpkg_lookup(const char *map_path, const char *name, osr_winpkg_spec *out);

/* osr_winpkg_have_command -- 1 if `name` resolves on PATH (+ .exe/.cmd/.bat
 * on Windows), else 0. The C equivalent of pkg.ps1's Test-Command.
 */
int osr_winpkg_have_command(const char *name);

/* osr_winpkg_available_managers -- which of scoop/choco/winget are on PATH. */
void osr_winpkg_available_managers(int *has_scoop, int *has_choco, int *has_winget);

/* osr_winpkg_install -- install `name` (test_command defaults to `name`
 * itself when NULL) via whichever available manager has a windows.map
 * entry for it, tried in scoop -> choco -> winget order (mirrors pkg.ps1).
 * Idempotent: returns 1 immediately if test_command is already on PATH.
 * Returns 1 on success (already-installed counts as success), 0 otherwise.
 */
int osr_winpkg_install(const char *map_path, const char *name, const char *test_command);

#endif /* OSR_WINPKG_H */
