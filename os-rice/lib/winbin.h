/* lib/winbin.h -- the toolkit a `source:` builder is written against.
 *
 * windows.map's providers are scoop/choco/winget (a manager installs it),
 * script: (run the vendor's own installer script) and source: (a C function
 * in provide_module.c does whatever it takes). This file is what those
 * source: functions call: fetch a release asset, unpack it, find the
 * executable, put a directory on PATH, hand a file to a vendor installer.
 *
 * Nothing here is a provider itself and nothing here is reached from the
 * map. That distinction matters: a provider is a decision the map makes,
 * while these are the moving parts a builder assembles. Adding a helper
 * here can never change how a package is resolved.
 *
 * Everything writes under %LOCALAPPDATA%\osr -- `bin\<name>` for installed
 * executables, `src\<name>` for source trees a builder compiles in -- and
 * PATH changes go to HKCU. So a builder needs no elevation unless it
 * chooses to run a system-wide installer (osr_winbin_run_installer).
 *
 * C89.
 */
#ifndef OSR_WINBIN_H
#define OSR_WINBIN_H

/* --- pure helpers: no I/O, unit-tested on any host ----------------------- */

/* osr_winbin_match -- glob match supporting `*` only, anchored at both ends.
 * Release assets are named with versions, so choosing one means globbing.
 */
int osr_winbin_match(const char *pattern, const char *text);

/* osr_winbin_pick_asset -- scan a GitHub releases JSON payload for the first
 * "browser_download_url" whose FILENAME matches `pattern` (never the whole
 * URL, so a pattern cannot be satisfied by the host or path). Returns 1 and
 * fills `out` on a match, else 0.
 */
int osr_winbin_pick_asset(const char *json, const char *pattern,
                          char *out, unsigned long out_sz);

/* What a downloaded artifact is. */
typedef enum {
    OSR_WINBIN_KIND_AUTO = 0,  /* infer from the filename extension */
    OSR_WINBIN_KIND_ZIP,       /* portable archive: unpack, put its dir on PATH */
    OSR_WINBIN_KIND_EXE,       /* the program itself: place it, put its dir on PATH */
    OSR_WINBIN_KIND_MSI,       /* Windows Installer package: msiexec /qn (admin) */
    OSR_WINBIN_KIND_SETUP      /* an installer .exe: run with silent switches (admin) */
} osr_winbin_kind;

/* osr_winbin_kind_of_file -- the kind implied by a filename's extension, or
 * AUTO when the extension says nothing useful. A bare .exe is taken to be
 * the program rather than an installer: that is what vendors publish beside
 * their archives, and a builder that means otherwise says so explicitly.
 */
osr_winbin_kind osr_winbin_kind_of_file(const char *filename);

/* osr_winbin_parse_spec -- split an artifact spec into kind, installer
 * arguments (SETUP only) and the source that remains. See
 * osr_winbin_artifact below for the grammar.
 */
int osr_winbin_parse_spec(const char *spec, osr_winbin_kind *kind,
                          char *args, unsigned long args_sz,
                          char *source, unsigned long source_sz);

/* --- locations ----------------------------------------------------------- */

/* osr_winbin_bin_dir -- %LOCALAPPDATA%\osr\bin\<name>, where installed
 * executables live. osr_winbin_src_dir -- %LOCALAPPDATA%\osr\src\<name>,
 * where a builder clones and compiles. Both return 1 on success.
 */
int osr_winbin_bin_dir(const char *name, char *out, unsigned long out_sz);
int osr_winbin_src_dir(const char *name, char *out, unsigned long out_sz);

/* --- fetching ------------------------------------------------------------ */

/* osr_winbin_resolve -- turn a source into a downloadable URL:
 *
 *   https://host/path/file.zip     used as-is
 *   gh:<owner>/<repo>:<pattern>    newest GitHub release of <owner>/<repo>,
 *                                  first asset whose filename matches
 *                                  <pattern> -- for version-named assets
 */
int osr_winbin_resolve(const char *source, char *url_out, unsigned long url_sz);

/* osr_winbin_fetch -- download `url` into the temp directory, writing the
 * resulting path to `path_out`. Returns 1 on success.
 */
int osr_winbin_fetch(const char *url, char *path_out, unsigned long path_sz);

/* --- unpacking and placing ----------------------------------------------- */

/* osr_winbin_unzip -- expand `archive` into `dest_dir` (created if needed).
 * Uses Expand-Archive, falling back to Shell.Application on hosts too old
 * for it. Returns 1 on success.
 */
int osr_winbin_unzip(const char *archive, const char *dest_dir);

/* osr_winbin_find_exe_dir -- depth-limited search for `exe_name` under
 * `root`, yielding the directory that holds it. Release archives disagree
 * about whether they wrap their contents in a version-named folder, so the
 * layout cannot be assumed.
 */
int osr_winbin_find_exe_dir(const char *root, const char *exe_name, int depth,
                            char *out, unsigned long out_sz);

/* osr_winbin_place -- copy `src_file` to `dest_dir\<exe_name>`, creating
 * the directory. Returns 1 on success.
 */
int osr_winbin_place(const char *src_file, const char *dest_dir, const char *exe_name);

/* osr_winbin_file_exists -- 1 if `path` names an existing file. Builders use
 * it to spot a reusable checkout or a binary the compiler produced.
 */
int osr_winbin_file_exists(const char *path);

/* osr_winbin_add_to_path -- append `dir` to the user's PATH (HKCU) if it is
 * not already there, tell running shells, and add it to this process so a
 * just-installed command resolves without a new shell.
 */
void osr_winbin_add_to_path(const char *dir);

/* --- running ------------------------------------------------------------- */

/* osr_winbin_run_installer -- hand `file` to a vendor installer: msiexec for
 * MSI, the file itself with `args` for SETUP. Both write outside the user's
 * tree, so both elevate first (one prompt per run, see elevate.h) and return
 * 0 if that is declined.
 */
int osr_winbin_run_installer(osr_winbin_kind kind, const char *file,
                             const char *args, const char *name);

/* osr_winbin_run_script -- fetch `url` and run it through PowerShell, the
 * route a vendor means by "paste this line into a shell". This is what the
 * map's `script:` provider uses. Per-user: no elevation is requested, since
 * an installer script that needs admin belongs behind a source: builder that
 * can say so. Returns 1 when the script exits 0.
 */
int osr_winbin_run_script(const char *url, const char *name);

/* --- the common shape, in one call --------------------------------------- */

/* osr_winbin_artifact -- fetch one artifact and install it, the body most
 * simple builders would otherwise repeat. `spec` is `[<kind>:]<source>`,
 * where <source> is as osr_winbin_resolve accepts and <kind> is inferred
 * from the file extension unless written (SETUP carries its silent switches
 * comma-separated after the kind: `setup,/S:<url>`).
 *
 * ZIP and EXE land under %LOCALAPPDATA%\osr\bin\<name> and go on PATH; MSI
 * and SETUP are handed to the vendor's installer. Returns 1 when
 * `test_command` resolves afterwards.
 */
int osr_winbin_artifact(const char *spec, const char *name, const char *test_command);

#endif /* OSR_WINBIN_H */
