/* lib/build.h -- the `source:` provider builders, and the toolkit they are
 * written against.
 *
 * A builder installs one program a native package cannot provide on some
 * target (DESIGN section 4), and is named by a pkgmap row:
 * `lsd@jammy = source:provide_lsd_deb`, `wezterm@arm64 = source:provide_wezterm`.
 * In sh those names were shell functions in scope, so lib/pkg.c had to shell
 * out for the whole row; here they are a table, and lib/pkg.c looks the name
 * up first. The same table serves both systems -- a `source:` row in
 * lib/pkgmap/apt.map and one in lib/pkgmap/windows.map reach it the same way,
 * and each entry is compiled for whichever system it has a recipe for.
 *
 * A builder returns 1 for success. The failure paths lib/build.sh spelled
 * `error ...` are osr_die on the POSIX side, same as there: a half-installed
 * program is not something to limp on from.
 *
 * C89 + POSIX, and C89 + Win32.
 */
#ifndef OSR_BUILD_H
#define OSR_BUILD_H

#include "common.h"

/* osr_build_has -- is there a builder of this name in this build? */
int osr_build_has(const char *fn);

/* osr_build_run -- run it. Undefined for a name osr_build_has rejects. */
int osr_build_run(const char *fn);

/* osr_build_needs_admin -- does this builder's recipe end in a system-wide
 * installer? Only the builder knows, and guessing from the outside would be
 * wrong in both directions, so it is declared per entry in the registry.
 * lib/pkg.c asks before any work starts, so that the one privilege prompt a
 * run needs happens up front rather than halfway through a compile -- which is
 * the same reason install.sh warms its sudo credential at the top. */
int osr_build_needs_admin(const char *fn);

/* --- the artifact helpers --------------------------------------------------
 *
 * Which release asset to take, what kind of file it is, how to read a spec.
 * Pure string work with no I/O, so the rules that decide WHICH file gets
 * downloaded and WHAT it is can be asserted without a network, on any host.
 * ------------------------------------------------------------------------ */

/* osr_glob_match -- glob match supporting `*` only, anchored at both ends.
 * Release assets are named with versions, so choosing one means globbing. */
int osr_glob_match(const char *pattern, const char *text);

/* osr_pick_release_asset -- scan a GitHub releases JSON payload for the first
 * "browser_download_url" whose FILENAME matches `pattern` -- never the whole
 * URL, so a pattern cannot be satisfied by the host or the path. Returns 1 and
 * fills `out` on a match, else 0. */
int osr_pick_release_asset(const char *json, const char *pattern,
                           char *out, unsigned long out_sz);

/* What a downloaded artifact is. */
typedef enum {
    OSR_ARTIFACT_AUTO = 0,  /* infer from the filename extension */
    OSR_ARTIFACT_ZIP,       /* portable archive: unpack, put its dir on PATH */
    OSR_ARTIFACT_EXE,       /* the program itself: place it, put its dir on PATH */
    OSR_ARTIFACT_MSI,       /* Windows Installer package: msiexec /qn (admin) */
    OSR_ARTIFACT_SETUP      /* an installer .exe: run with silent switches (admin) */
} osr_artifact_kind;

/* osr_artifact_kind_of_file -- the kind implied by a filename's extension, or
 * AUTO when the extension says nothing useful. A bare .exe is taken to be the
 * program rather than an installer: that is what vendors publish beside their
 * archives, and a builder that means otherwise says so explicitly. */
osr_artifact_kind osr_artifact_kind_of_file(const char *filename);

/* osr_parse_artifact_spec -- split an artifact spec into kind, installer
 * arguments (SETUP only) and the source that remains. See osr_install_artifact
 * for the grammar. */
int osr_parse_artifact_spec(const char *spec, osr_artifact_kind *kind,
                            char *args, unsigned long args_sz,
                            char *source, unsigned long source_sz);

/* --- the POSIX toolkit ----------------------------------------------------- */
#ifndef _WIN32

/* osr_install_tarball_bin -- fetch a release tarball, find the named binary
 * anywhere inside it, install it 0755 into /usr/local/bin. dpkg-free, so it
 * works where a modern zstd .deb cannot (bullseye's dpkg lacks zstd). */
int osr_install_tarball_bin(const char *url, const char *bin);

/* --- version guards a module makes before calling a builder ----------------
 *
 * Two builders exist because PRESENCE IS NOT SUFFICIENCY: an old distro chafa
 * or fzf satisfies pkg_install's "is it installed" probe and would never be
 * replaced, and the feature the rice needs (chafa --probe, fzf --gutter) is
 * missing anyway. The module asks first, so the guard costs one `--version`
 * run rather than a builder invocation, exactly as the sh modules' `_chafa_ok`
 * / `_fzf_ok` did. The MIN strings are exported because the modules name them
 * in the step they print. */
#define OSR_CHAFA_MIN "1.16"
#define OSR_FZF_MIN   "0.66"
int osr_chafa_ok(void);
int osr_fzf_ok(void);

/* osr_lsd_ok -- the third face of "presence is not sufficiency", and the one
 * that is not about a version: a distro lsd links the distro libgit2, which
 * links libssh2, so lsd stops at the dynamic loader the moment anything down
 * that chain goes missing. It is still installed, `command -v` still finds it,
 * and pkg_install would never touch it again -- but 20-aliases.zsh aliases ls
 * to it, so EVERY `ls` in every shell answers with a loader error. Nothing here
 * asks how new it is; asking whether it RUNS covers a broken link and an absent
 * binary alike. */
int osr_lsd_ok(void);

/* osr_build_zig -- install Zig from ziglang.org as a whole tree, symlinked into
 * /usr/local/bin. want pins an exact version ("0.14.1"); "" or NULL takes the
 * newest stable. Exposed because it is also a PREREQUISITE: the ghostty source
 * build reads the exact Zig version ghostty pins and asks for that one (G1, a
 * source: builder with a bootstrapped toolchain under it). */
int osr_build_zig(const char *want);

#else /* _WIN32 */

/* --- the Windows toolkit ---------------------------------------------------
 *
 * Everything here writes under %LOCALAPPDATA%\osr -- `bin\<name>` for
 * installed executables, `src\<name>` for source trees a builder compiles in
 * -- and PATH changes go to HKCU, so a builder needs no elevation unless it
 * chooses to run a system-wide installer.
 *
 * Nothing here is a provider and nothing here is reached from a map row: a
 * provider is a decision the map makes, while these are the moving parts a
 * builder assembles out of. Adding a helper can never change how a package
 * resolves.
 * ------------------------------------------------------------------------ */

/* osr_bin_dir -- %LOCALAPPDATA%\osr\bin\<name>, where installed executables
 * live. osr_src_dir -- %LOCALAPPDATA%\osr\src\<name>, where a builder clones
 * and compiles. Both return 1 on success. */
int osr_bin_dir(const char *name, char *out, unsigned long out_sz);
int osr_src_dir(const char *name, char *out, unsigned long out_sz);

/* osr_artifact_url -- turn a source into a downloadable URL:
 *
 *   https://host/path/file.zip     used as-is
 *   gh:<owner>/<repo>:<pattern>    newest GitHub release of <owner>/<repo>,
 *                                  first asset whose filename matches
 *                                  <pattern> -- for version-named assets
 */
int osr_artifact_url(const char *source, char *url_out, unsigned long url_sz);

/* osr_fetch_artifact -- download `url` into the temp directory, writing the
 * resulting path to `path_out`. Returns 1 on success. */
int osr_fetch_artifact(const char *url, char *path_out, unsigned long path_sz);

/* osr_unzip -- expand `archive` into `dest_dir` (created if needed). Uses
 * Expand-Archive, falling back to Shell.Application on hosts too old for it. */
int osr_unzip(const char *archive, const char *dest_dir);

/* osr_find_exe_dir -- depth-limited search for `exe_name` under `root`,
 * yielding the directory that holds it. Release archives disagree about
 * whether they wrap their contents in a version-named folder, so the layout
 * cannot be assumed. */
int osr_find_exe_dir(const char *root, const char *exe_name, int depth,
                     char *out, unsigned long out_sz);

/* osr_place_binary -- copy `src_file` to `dest_dir\<exe_name>`, creating the
 * directory. Returns 1 on success. */
int osr_place_binary(const char *src_file, const char *dest_dir, const char *exe_name);

/* osr_add_to_path -- append `dir` to the user's PATH (HKCU) if it is not
 * already there, tell running shells, and add it to this process so a
 * just-installed command resolves without a new shell. */
void osr_add_to_path(const char *dir);

/* osr_run_installer -- hand `file` to a vendor installer: msiexec for MSI, the
 * file itself with `args` for SETUP. Both write outside the user's tree, so
 * both elevate first (one prompt per run, see elevate.h) and return 0 if that
 * is declined. */
int osr_run_installer(osr_artifact_kind kind, const char *file,
                      const char *args, const char *name);

/* osr_run_install_script -- fetch `url` and run it through PowerShell, the
 * route a vendor means by "paste this line into a shell". This is what a map's
 * `script:` provider uses. Per-user: no elevation is requested, since an
 * installer script that needs admin belongs behind a source: builder that can
 * say so. Returns 1 when the script exits 0. */
int osr_run_install_script(const char *url, const char *name);

/* osr_install_artifact -- fetch one artifact and install it, the body most
 * simple builders would otherwise repeat. `spec` is `[<kind>:]<source>`, where
 * <source> is as osr_artifact_url accepts and <kind> is inferred from the file
 * extension unless written (SETUP carries its silent switches comma-separated
 * after the kind: `setup,/S:<url>`).
 *
 * ZIP and EXE land under %LOCALAPPDATA%\osr\bin\<name> and go on PATH; MSI and
 * SETUP are handed to the vendor's installer. Returns 1 when `test_command`
 * resolves afterwards. */
int osr_install_artifact(const char *spec, const char *name, const char *test_command);

/* OSR_CMD_MAX -- how long a command line a builder composes. Paths are
 * OSR_PATH_MAX (lib/common.h); a command line holds two of them plus flags. */
#define OSR_CMD_MAX 2048

#endif /* _WIN32 */

#endif /* OSR_BUILD_H */
