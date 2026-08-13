/* lib/winbin.h -- install a package from the vendor's own release binary,
 * with no package manager involved.
 *
 * `bin:` is one of windows.map's providers, on equal footing with scoop,
 * choco and winget -- one per row, like all of them (see winpkg.h). It is
 * not a fallback attached to a manager row: a package either comes from a
 * manager on this machine or it comes from here, and the map says which,
 * per @facet, in advance.
 *
 * When to reach for it: the manager a package is pinned to has no build for
 * this machine -- most often an architecture it never shipped. It is worth
 * saying what it costs, because that is why it is not the default. A
 * manager knows how to upgrade and remove what it installed; this does not.
 * A re-run overwrites in place, which is idempotent but is not version
 * tracking, and nothing here ever uninstalls. So a row that names `bin:`
 * where a manager would serve is a downgrade, not a simplification.
 *
 * What it is NOT is a second namespace to guess in: a `bin:` spec is an
 * explicit URL, or an explicit owner/repo, written in the map by hand. The
 * name-collision problem that made the old scoop->choco->winget fallback
 * unsafe does not apply, because nothing here is resolved by name.
 *
 * Spec grammar (the `bin:` token's value in windows.map):
 *
 *   [<kind>:]<source>
 *
 * where <source> is either
 *
 *   https://host/path/file.zip     a direct URL
 *   gh:<owner>/<repo>:<pattern>    newest GitHub release of <owner>/<repo>,
 *                                  first asset whose filename matches
 *                                  <pattern> (a glob, `*` only) -- needed
 *                                  where the asset name carries a version,
 *                                  e.g. PowerShell-*-win-x64.zip
 *
 * and <kind> says what the downloaded file IS. It is inferred from the
 * filename extension when omitted, which covers every row today; write it
 * explicitly only when the extension is absent or lies:
 *
 *   zip    portable archive: expanded under %LOCALAPPDATA%\osr\bin\<name>\,
 *          and the directory holding the executable goes on the user's PATH
 *   exe    the program itself: placed under that directory as <command>.exe
 *          (vendors name these for the build -- posh-windows-amd64.exe --
 *          not for the command they provide)
 *   msi    a Windows Installer package: `msiexec /i ... /qn /norestart`.
 *          Per-machine, so this one DOES need Administrator; it elevates
 *          once through lib/elevate.h like any other privileged step
 *   setup  an installer .exe rather than the program: run with the silent
 *          switches the row supplies, since every installer toolkit spells
 *          them differently. Written `setup,/S:<source>` -- the switches
 *          follow the kind, comma-separated, and become arguments
 *
 * zip and exe are per-user throughout: no manager, no elevation, nothing
 * written outside %LOCALAPPDATA% and HKCU. msi and setup hand off to a
 * vendor installer, so where those put things is the vendor's choice.
 *
 * C89.
 */
#ifndef OSR_WINBIN_H
#define OSR_WINBIN_H

/* osr_winbin_install -- install `name` from `spec` (the grammar above),
 * placing an executable called `test_command` on PATH. Returns 1 when
 * `test_command` resolves afterwards, 0 on any failure (each of which
 * warns on its own).
 */
int osr_winbin_install(const char *spec, const char *name, const char *test_command);

/* What a downloaded artifact is -- see the grammar above. */
typedef enum {
    OSR_WINBIN_KIND_AUTO = 0,  /* infer from the filename extension */
    OSR_WINBIN_KIND_ZIP,
    OSR_WINBIN_KIND_EXE,
    OSR_WINBIN_KIND_MSI,
    OSR_WINBIN_KIND_SETUP
} osr_winbin_kind;

/* osr_winbin_parse_spec -- split a bin: spec into its kind, the installer
 * arguments (setup only; "" otherwise), and the source that remains. Pure,
 * and exposed for the unit tests: a spec that parsed wrongly would download
 * the right file and then treat it as the wrong kind of thing. Returns 1 on
 * success, 0 if the spec is empty or does not fit the output buffers.
 */
int osr_winbin_parse_spec(const char *spec, osr_winbin_kind *kind,
                          char *args, unsigned long args_sz,
                          char *source, unsigned long source_sz);

/* osr_winbin_kind_of_file -- the kind implied by a filename's extension,
 * or OSR_WINBIN_KIND_AUTO when the extension says nothing useful.
 */
osr_winbin_kind osr_winbin_kind_of_file(const char *filename);

/* osr_winbin_match -- glob match supporting `*` only, anchored at both
 * ends. Exposed for the unit tests; asset selection depends on it.
 */
int osr_winbin_match(const char *pattern, const char *text);

/* osr_winbin_pick_asset -- scan a GitHub releases JSON payload for the
 * first "browser_download_url" whose filename matches `pattern`. Pure
 * string handling, no I/O, so the selection rule is unit-testable without
 * a network. Returns 1 and fills `out` on a match, else 0.
 */
int osr_winbin_pick_asset(const char *json, const char *pattern,
                          char *out, unsigned long out_sz);

#endif /* OSR_WINBIN_H */
