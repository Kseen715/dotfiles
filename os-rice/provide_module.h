/* provide_module.h -- source: provider functions, the C port of lib/build.sh.
 *
 * Each builder installs a program no package manager can provide on some
 * target: an architecture the manager never shipped, a program that has to
 * be compiled, an upstream that publishes only from somewhere obscure. A
 * windows.map row reaches one by name -- `wezterm@arm64 = source:provide_wezterm`
 * -- exactly as lib/pkgmap's rows reach the shell functions in lib/build.sh.
 *
 * Why a function and not a URL: a URL can only ever say "download this".
 * A builder can resolve a version, pick an asset per architecture, install
 * its own build dependencies through the map, clone with submodules, run a
 * compiler, retry a different way, and place several binaries at the end.
 * The map row stays logic-free either way -- the logic lives here, in one
 * named place per package, where it can be read.
 *
 * Contract, copied from lib/pkg.sh's _via_source so both platforms behave
 * the same way:
 *
 *   - Idempotency belongs to the DISPATCHER, not the builder:
 *     osr_provide_run probes `test_command` first and skips a builder whose
 *     program is already present. A builder may therefore assume it is only
 *     called when there is work to do.
 *   - An unknown builder name is a map error, reported as such, never a
 *     silent skip.
 *   - A builder returns 1 on success and 0 on failure, warning on its way
 *     out so the reason reaches the user.
 *
 * Builders are written against lib/winbin.h (fetch, unpack, place, PATH)
 * the way lib/build.sh's are written against osr_download / _osr_install_*.
 * They receive `map_path` so they can install their own dependencies with
 * osr_winpkg_install, which is how provide_wezterm gets git and rust --
 * lib/build.sh's provide_wezterm calls `pkg_install build git` for the same
 * reason.
 *
 * ADDING ONE: write a provide/<name>.c defining a single
 * `static int provide_<name>(...)`, #include it in provide_module.c's
 * metapacket block, add a row to the registry there, and point a map row at
 * it. The per-package file keeps each recipe readable on its own, and the
 * metapacket keeps them one translation unit, so a builder costs no build
 * plumbing.
 *
 * C89.
 */
#ifndef OSR_PROVIDE_MODULE_H
#define OSR_PROVIDE_MODULE_H

/* Buffer sizes for builders. Deliberately not MAX_PATH: a builder never
 * includes windows.h -- everything platform-specific it needs comes through
 * lib/winbin.h -- which keeps the builder sources compiling on any host
 * and the registry testable off Windows.
 */
#define OSRP_PATH_MAX 600
#define OSRP_CMD_MAX  900

/* osr_provide_fn -- a builder. `map_path` is windows.map (for installing
 * dependencies), `name` the logical package name, `test_command` the
 * command that must resolve when it is done.
 */
typedef int (*osr_provide_fn)(const char *map_path, const char *name,
                              const char *test_command);

/* osr_provide_known -- 1 if `fn_name` names a builder in the registry. */
int osr_provide_known(const char *fn_name);

/* osr_provide_needs_admin -- 1 if this builder is known to require
 * Administrator (it runs a system-wide installer). Lets install.c ask for
 * elevation once, up front, instead of halfway through a build.
 */
int osr_provide_needs_admin(const char *fn_name);

/* osr_provide_run -- run the named builder. Returns 1 if `test_command`
 * resolves afterwards (including when it already did and the build was
 * skipped), 0 if the name is unknown or the build failed.
 */
int osr_provide_run(const char *fn_name, const char *map_path, const char *name,
                    const char *test_command);

#endif /* OSR_PROVIDE_MODULE_H */
