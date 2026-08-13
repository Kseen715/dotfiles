/* lib/winpkg.h -- windows.map lookup + single-manager install dispatch.
 *
 * windows.map names, for each logical package, the ONE package manager that
 * owns it (`name[@facet] = <mgr>:<id>`, mgr in {scoop,choco,winget}). This
 * file resolves that row and installs from that manager alone.
 *
 * The one-manager rule is a trust boundary, not a preference. scoop, choco
 * and winget are separate namespaces: `foo` in one is not `foo` in another,
 * and the manager the project does not ship to is exactly where a name is
 * free for someone else to take. The earlier design here tried scoop, then
 * choco, then winget until one succeeded, which turned "the intended manager
 * is missing" into "install whatever the next namespace has under this name".
 * Now a missing manager is bootstrapped (osr_winpkg_install below) rather
 * than substituted, and a row naming more than one manager is a map error.
 *
 * Facet qualifiers mirror lib/pkg.sh's (DESIGN §1a): the most specific key
 * wins, release > version > arch > bare name. osr_winpkg_lookup takes the
 * facets as a parameter rather than detecting them, which keeps it a pure
 * function of (file, name, facets) -- unit-testable on any host.
 *
 * C89.
 */
#ifndef OSR_WINPKG_H
#define OSR_WINPKG_H

#define OSR_WINPKG_ID_MAX  96   /* max bytes for one manager's package id */
#define OSR_WINPKG_KEY_MAX 128  /* max bytes for a `name@facet` map key */
#define OSR_WINPKG_BIN_MAX 256  /* max bytes for a bin: route spec */

typedef enum {
    OSR_WINPKG_MGR_NONE = 0,
    OSR_WINPKG_MGR_SCOOP,
    OSR_WINPKG_MGR_CHOCO,
    OSR_WINPKG_MGR_WINGET
} osr_winpkg_mgr;

typedef enum {
    OSR_WINPKG_OK = 0,
    OSR_WINPKG_NOT_FOUND,  /* no row for this name (under any candidate key) */
    OSR_WINPKG_BAD_ROW     /* row found but malformed -- see osr_winpkg_lookup */
} osr_winpkg_status;

typedef struct {
    osr_winpkg_mgr mgr;              /* MGR_NONE for a bin-only row */
    char id[OSR_WINPKG_ID_MAX];      /* manager's own id, e.g. Microsoft.PowerShell */
    char bucket[OSR_WINPKG_ID_MAX];  /* scoop only: bucket from `bucket/name`, else "" */
    char bin[OSR_WINPKG_BIN_MAX];    /* optional vendor-binary route, "" if none */
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
 * A row names exactly one provider: `<mgr>:<id>` or `bin:<spec>` (winbin.h).
 * Returns OSR_WINPKG_OK and fills *out; OSR_WINPKG_NOT_FOUND if no candidate
 * key has a row (or the file cannot be opened); OSR_WINPKG_BAD_ROW if the
 * winning row is empty, carries MORE THAN ONE provider (the old
 * fallback-chain format -- rejected rather than silently reduced to its
 * first token, and that includes a manager paired with a bin: route), names
 * a manager other than scoop/choco/winget, or has an empty id. BAD_ROW is
 * deliberately not a fallthrough to a less specific row: a map error must be
 * fixed, not routed around.
 */
int osr_winpkg_lookup(const char *map_path, const char *name,
                      const osr_winpkg_facets *facets, osr_winpkg_spec *out);

/* osr_winpkg_mgr_name -- "scoop" | "choco" | "winget" | "none". */
const char *osr_winpkg_mgr_name(osr_winpkg_mgr mgr);

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
 *   a manager row   use that manager. If this machine does not have it,
 *                   install it from its own vendor installer first: scoop
 *                   from get.scoop.sh (per-user, no elevation), choco from
 *                   community.chocolatey.org/install.ps1, winget from
 *                   asheroto/winget-install. The last two need
 *                   Administrator, so the run elevates once (elevate.h)
 *                   and continues elevated. A missing manager is
 *                   *installed*, never substituted.
 *
 *   a bin: row      use the vendor's own artifact directly (winbin.h).
 *                   This is what a row says when no manager should serve
 *                   the package on this machine -- typically a per-@facet
 *                   row for an architecture the manager has no build for.
 *
 * If the named provider fails, the install fails. Falling through to
 * another provider is exactly the behaviour decision 10 removed: it turns
 * "the intended source is unavailable" into "install from wherever else
 * this name happens to exist", which is a different publisher's software.
 * Where a package must come from somewhere else on some machines, the map
 * says so with an @facet row, deliberately, in advance.
 */
int osr_winpkg_install(const char *map_path, const char *name, const char *test_command);

/* osr_winpkg_ensure_manager -- 1 if `mgr` is on PATH, installing it from its
 * vendor installer if not. scoop installs per-user with no elevation; choco's
 * and winget's installers both require Administrator, so those elevate first
 * (one prompt per run, see elevate.h) and return 0 if that is declined.
 */
int osr_winpkg_ensure_manager(osr_winpkg_mgr mgr);

/* osr_winpkg_refresh_env -- re-read Machine + User environment variables
 * (including PATH) from the registry into this process, so something just
 * installed is visible without a new shell. The C port of common.ps1's
 * Update-SessionEnvironment.
 */
void osr_winpkg_refresh_env(void);

/* osr_winpkg_run_needs_admin -- would installing these names require
 * elevation? True for a name whose pinned manager is missing and whose
 * installer needs Administrator. install.c asks this once before doing any
 * work, so the UAC prompt happens up front rather than partway through --
 * the same reason install.sh warms the sudo credential at the top of a run
 * instead of mid-loop.
 */
int osr_winpkg_run_needs_admin(const char *map_path, char **names, int count);

#endif /* OSR_WINPKG_H */
