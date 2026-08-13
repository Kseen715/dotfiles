/* lib/winpkg.h -- windows.map lookup + single-provider install dispatch.
 *
 * windows.map names, for each logical package, the ONE provider that
 * installs it here (`name[@facet] = <provider>`). A provider is a manager
 * (`scoop:`/`choco:`/`winget:` + that manager's id), a builder
 * (`source:provide_x`, see provide_module.h), or a vendor install script
 * (`script:<url>`) -- the same set lib/pkg.sh dispatches on Linux, minus
 * the ones with no Windows meaning.
 *
 * The one-provider rule is a trust boundary, not a preference. scoop, choco
 * and winget are separate namespaces: `foo` in one is not `foo` in another,
 * and the manager a project does not ship to is exactly where its name is
 * free for someone else to take. This file once tried scoop, then choco,
 * then winget until one succeeded, which turned "the intended manager is
 * missing" into "install whatever the next namespace has under this name".
 * Now a row names one provider, a row naming two is a map error, and a
 * missing manager is *installed* from its own vendor installer rather than
 * substituted.
 *
 * Facet qualifiers mirror lib/pkg.sh's (DESIGN §1a): the most specific key
 * wins, release > version > arch > bare name. That is how a package changes
 * provider per machine -- `wezterm@arm64 = source:provide_wezterm` where the
 * manager has no build -- deliberately and in advance, never as a runtime
 * scramble. osr_winpkg_lookup takes the facets as a parameter rather than
 * detecting them, which keeps it a pure function of (file, name, facets),
 * unit-testable on any host.
 *
 * C89.
 */
#ifndef OSR_WINPKG_H
#define OSR_WINPKG_H

#define OSR_WINPKG_ID_MAX  96   /* max bytes for one manager's package id */
#define OSR_WINPKG_KEY_MAX 128  /* max bytes for a `name@facet` map key */
#define OSR_WINPKG_ARG_MAX 256  /* max bytes for a source:/script: argument */

/* The providers a row may name. The three managers install by id; source
 * runs a builder in provide_module.c; script runs the vendor's own install
 * script. Mirrors lib/pkg.sh's native/script/source set. */
typedef enum {
    OSR_WINPKG_PROV_NONE = 0,
    OSR_WINPKG_PROV_SCOOP,
    OSR_WINPKG_PROV_CHOCO,
    OSR_WINPKG_PROV_WINGET,
    OSR_WINPKG_PROV_SOURCE,
    OSR_WINPKG_PROV_SCRIPT
} osr_winpkg_provider;

/* osr_winpkg_is_manager -- 1 for scoop/choco/winget, which install by name
 * from a namespace; 0 for source/script, which do not. */
int osr_winpkg_is_manager(osr_winpkg_provider provider);

typedef enum {
    OSR_WINPKG_OK = 0,
    OSR_WINPKG_NOT_FOUND,  /* no row for this name (under any candidate key) */
    OSR_WINPKG_BAD_ROW     /* row found but malformed -- see osr_winpkg_lookup */
} osr_winpkg_status;

typedef struct {
    osr_winpkg_provider provider;
    char id[OSR_WINPKG_ARG_MAX];     /* manager id, builder name, or script URL */
    char bucket[OSR_WINPKG_ID_MAX];  /* scoop only: bucket from `bucket/name`, else "" */
    char key[OSR_WINPKG_KEY_MAX];    /* the key that matched, for diagnostics */
} osr_winpkg_spec;

/* osr_winpkg_facets -- this machine's facet values, "" when unknown (an
 * empty facet contributes no candidate key). Windows analogues of
 * OSR_CODENAME / OSR_VERSION_ID / OSR_ARCH.
 */
typedef struct {
    char release[32];  /* DisplayVersion, e.g. "24H2" */
    char version[32];  /* product major, "11" or "10" */
    char arch[32];     /* "x86_64" | "arm64" | "x86" */
} osr_winpkg_facets;

/* osr_winpkg_detect_facets -- fill `out` from the running system. All fields
 * are "" off Windows, so lookup there considers the bare name only.
 */
void osr_winpkg_detect_facets(osr_winpkg_facets *out);

/* osr_winpkg_lookup -- resolve `name` in the windows.map at map_path.
 * `facets` may be NULL (bare name only). Pure parsing, no OS dependency.
 *
 * Candidate keys, most specific first: name@release, name@version, name@arch,
 * name. The first key with a row wins outright -- a more specific row is never
 * merged with a less specific one.
 *
 * A row names exactly one provider: `<mgr>:<id>`, `source:<builder>` or
 * `script:<url>`. Returns OSR_WINPKG_OK and fills *out; OSR_WINPKG_NOT_FOUND
 * if no candidate key has a row (or the file cannot be opened);
 * OSR_WINPKG_BAD_ROW if the winning row is empty, carries MORE THAN ONE
 * provider (the old fallback-chain format -- rejected rather than silently
 * reduced to its first token), names an unknown provider, or has an empty
 * argument. BAD_ROW is deliberately not a fallthrough to a less specific
 * row: a map error must be fixed, not routed around.
 */
int osr_winpkg_lookup(const char *map_path, const char *name,
                      const osr_winpkg_facets *facets, osr_winpkg_spec *out);

/* osr_winpkg_provider_name -- "scoop" | "choco" | "winget" | "source" |
 * "script" | "none". For the three managers this is also the command name. */
const char *osr_winpkg_provider_name(osr_winpkg_provider provider);

/* osr_winpkg_have_command -- 1 if `name` resolves on PATH (+ .exe/.cmd/.bat
 * on Windows), else 0.
 */
int osr_winpkg_have_command(const char *name);

/* osr_winpkg_install -- install `name` (test_command defaults to `name` when
 * NULL). Idempotent: returns 1 immediately when test_command is already on
 * PATH. Returns 1 on success, 0 if the row is missing/malformed or every
 * route available to this machine failed.
 *
 * The row names one provider and that provider is used -- there is no
 * second choice at runtime:
 *
 *   scoop/choco/winget   install by id from that manager. If this machine
 *                        does not have the manager, install it from its own
 *                        vendor installer first: scoop from get.scoop.sh
 *                        (per-user, no elevation), choco from
 *                        community.chocolatey.org/install.ps1, winget from
 *                        asheroto/winget-install. The last two need
 *                        Administrator, so the run elevates once (elevate.h)
 *                        and continues elevated. A missing manager is
 *                        *installed*, never substituted.
 *
 *   source:<builder>     run the named builder in provide_module.c, which
 *                        does whatever the package actually takes: resolve a
 *                        version, install its own build dependencies, clone,
 *                        compile, place several binaries. This is what a row
 *                        says when no manager can serve the package here --
 *                        typically a per-@facet row for an architecture
 *                        upstream never shipped.
 *
 *   script:<url>         fetch the vendor's own install script and run it,
 *                        the route a project means by "paste this line into
 *                        a shell". Per-user; a script needing admin belongs
 *                        behind a source: builder that can say so.
 *
 * If the named provider fails, the install fails. Falling through to another
 * provider is exactly the behaviour decision 10 removed: it turns "the
 * intended source is unavailable" into "install from wherever else this name
 * happens to exist", which is a different publisher's software.
 */
int osr_winpkg_install(const char *map_path, const char *name, const char *test_command);

/* osr_winpkg_ensure_manager -- 1 if `mgr` is on PATH, installing it from its
 * vendor installer if not. scoop installs per-user with no elevation; choco's
 * and winget's installers both require Administrator, so those elevate first
 * (one prompt per run, see elevate.h) and return 0 if that is declined.
 */
int osr_winpkg_ensure_manager(osr_winpkg_provider provider);

/* osr_winpkg_refresh_env -- re-read Machine + User environment variables
 * (including PATH) from the registry into this process, so something just
 * installed is visible without a new shell. The C port of common.ps1's
 * Update-SessionEnvironment.
 */
void osr_winpkg_refresh_env(void);

/* osr_winpkg_run_needs_admin -- would installing these names require
 * elevation? True for a name whose manager is missing and whose installer
 * needs Administrator, or whose source: builder declares that it does. install.c asks this once before doing any
 * work, so the UAC prompt happens up front rather than partway through --
 * the same reason install.sh warms the sudo credential at the top of a run
 * instead of mid-loop.
 */
int osr_winpkg_run_needs_admin(const char *map_path, char **names, int count);

#endif /* OSR_WINPKG_H */
