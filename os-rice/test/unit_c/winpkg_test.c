/* test/unit_c/winpkg_test.c -- lib/winpkg.c's windows.map lookup.
 * Platform-independent: osr_winpkg_lookup is plain fopen/fgets and takes its
 * facets as a parameter, so the whole resolver runs on any host.
 *
 * Two maps are exercised. The real os-rice/windows.map, so drift between the
 * map and this test never goes unnoticed -- including the invariant that every
 * row names exactly one provider, and that every source: row points at a
 * builder provide_module.c actually registers. And
 * fixtures/windows_facets.map, for facet precedence, every provider spelling,
 * and the malformed rows the real map must never contain.
 */
#include "../c_test.h"
#include "../../lib/winpkg.h"
#include "../../provide_module.h"

#define MAP_PATH   "../../windows.map"
#define FIXTURE    "fixtures/windows_facets.map"

static osr_winpkg_facets facets_of(const char *release, const char *version, const char *arch) {
    osr_winpkg_facets f;
    memset(&f, 0, sizeof(f));
    strcpy(f.release, release);
    strcpy(f.version, version);
    strcpy(f.arch, arch);
    return f;
}

/* --- the real map ---------------------------------------------------------- */

static void check_real_row(const char *name, const osr_winpkg_facets *facets,
                           osr_winpkg_provider provider, const char *id) {
    osr_winpkg_spec spec;
    char label[128];

    sprintf(label, "windows.map: %s resolves", name);
    osr_t_eq_int(label, osr_winpkg_lookup(MAP_PATH, name, facets, &spec), OSR_WINPKG_OK);

    sprintf(label, "windows.map: %s provider", name);
    osr_t_eq_str(label, osr_winpkg_provider_name(spec.provider),
                 osr_winpkg_provider_name(provider));

    sprintf(label, "windows.map: %s argument", name);
    osr_t_eq_str(label, spec.id, id);
}

static void test_real_map(void) {
    osr_winpkg_facets x64 = facets_of("", "", "x86_64");
    osr_winpkg_facets arm = facets_of("", "", "arm64");
    osr_winpkg_spec spec;

    /* Each row's provider is the one that project's own Windows install page
     * names -- see the header of windows.map. */
    check_real_row("pwsh",       NULL, OSR_WINPKG_PROV_WINGET, "Microsoft.PowerShell");
    check_real_row("oh-my-posh", NULL, OSR_WINPKG_PROV_WINGET, "JanDeDobbeleer.OhMyPosh");
    check_real_row("starship",   NULL, OSR_WINPKG_PROV_WINGET, "Starship.Starship");
    check_real_row("fastfetch",  NULL, OSR_WINPKG_PROV_SCOOP,  "fastfetch");

    /* fastfetch is in scoop's main bucket, so it carries no bucket prefix --
     * if that ever changes the install must run `scoop bucket add` first. */
    osr_winpkg_lookup(MAP_PATH, "fastfetch", NULL, &spec);
    osr_t_eq_str("windows.map: fastfetch needs no bucket", spec.bucket, "");

    /* The build dependencies provide/wezterm.c installs through the map. */
    check_real_row("git",            NULL, OSR_WINPKG_PROV_WINGET, "Git.Git");
    check_real_row("rustup",         NULL, OSR_WINPKG_PROV_WINGET, "Rustlang.Rustup");
    check_real_row("strawberryperl", NULL, OSR_WINPKG_PROV_WINGET,
                   "StrawberryPerl.StrawberryPerl");

    /* wezterm is the worked example: winget on x64, a source build on arm64,
     * where upstream ships no binary for any manager to carry. */
    check_real_row("wezterm", &x64, OSR_WINPKG_PROV_WINGET, "wez.wezterm");
    check_real_row("wezterm", &arm, OSR_WINPKG_PROV_SOURCE, "provide_wezterm");
}

/* A source: row is a reference into provide_module.c's registry, and nothing
 * but a test cross-checks the two: a typo would parse perfectly and then fail
 * at install time with "builder not defined". */
static void test_real_map_builders_exist(void) {
    osr_winpkg_facets arm = facets_of("", "", "arm64");
    osr_winpkg_spec spec;

    osr_winpkg_lookup(MAP_PATH, "wezterm", &arm, &spec);
    osr_t_eq_int("builder: wezterm@arm64 names a source: row",
                 spec.provider, OSR_WINPKG_PROV_SOURCE);
    osr_t_true("builder: the name it uses is registered in provide_module.c",
               osr_provide_known(spec.id));

    osr_t_true("builder: an unregistered name is not known",
               !osr_provide_known("provide_definitely_not_real"));
    osr_t_true("builder: provide_wezterm needs no Administrator",
               !osr_provide_needs_admin("provide_wezterm"));
}

static void test_real_map_one_provider(void) {
    static const char *names[6] = { "pwsh", "oh-my-posh", "starship", "fastfetch",
                                    "git", "rustup" };
    osr_winpkg_spec spec;
    char label[128];
    int i;

    /* Every row resolves to exactly one provider. A row that grew a second
     * one would still have parsed under the old format, so this is checked
     * on the real map and not only on the fixture. */
    for (i = 0; i < 6; i++) {
        osr_t_eq_int("one provider: resolves",
                     osr_winpkg_lookup(MAP_PATH, names[i], NULL, &spec), OSR_WINPKG_OK);

        sprintf(label, "windows.map: %s names one provider", names[i]);
        osr_t_true(label, spec.provider != OSR_WINPKG_PROV_NONE);
    }
}

/* Architecture is the manager's job wherever the manager has the build --
 * verified against winget's and scoop's own manifests, which carry arm64
 * installers for all of these. An @arm64 row in front of a provider that
 * already resolves the architecture would be a downgrade, so these must have
 * none and must fall to their bare row on arm64.
 */
static void test_real_map_arch(void) {
    static const char *names[4] = { "pwsh", "oh-my-posh", "starship", "fastfetch" };
    osr_winpkg_facets arm = facets_of("", "", "arm64");
    osr_winpkg_spec spec;
    char label[128];
    int i;

    for (i = 0; i < 4; i++) {
        osr_t_eq_int("arm64: resolves", osr_winpkg_lookup(MAP_PATH, names[i], &arm, &spec),
                     OSR_WINPKG_OK);

        sprintf(label, "arm64: %s uses its bare row (no arch override)", names[i]);
        osr_t_eq_str(label, spec.key, names[i]);
    }

    /* wezterm is the exception, and has no bare row at all: upstream ships no
     * 32-bit build either, so an x86 machine must get "no row" rather than a
     * silently wrong answer. */
    osr_t_eq_int("x86: wezterm has no row to fall back on",
                 osr_winpkg_lookup(MAP_PATH, "wezterm", NULL, &spec), OSR_WINPKG_NOT_FOUND);
}

/* --- facet precedence ------------------------------------------------------ */

static void check_facet(const char *label, const osr_winpkg_facets *f,
                        const char *name, const char *expect_key, const char *expect_id) {
    osr_winpkg_spec spec;
    char buf[128];

    sprintf(buf, "facet: %s (resolves)", label);
    osr_t_eq_int(buf, osr_winpkg_lookup(FIXTURE, name, f, &spec), OSR_WINPKG_OK);

    sprintf(buf, "facet: %s (key)", label);
    osr_t_eq_str(buf, spec.key, expect_key);

    sprintf(buf, "facet: %s (id)", label);
    osr_t_eq_str(buf, spec.id, expect_id);
}

static void test_facet_precedence(void) {
    osr_winpkg_facets all  = facets_of("24H2", "11", "arm64");
    osr_winpkg_facets no_r = facets_of("",     "11", "arm64");
    osr_winpkg_facets arch = facets_of("",     "",   "arm64");
    osr_winpkg_facets miss = facets_of("23H2", "10", "x86_64");

    check_facet("release beats version/arch/bare", &all,  "demo", "demo@24H2",  "demo-release");
    check_facet("version beats arch/bare",         &no_r, "demo", "demo@11",    "Demo.Version");
    check_facet("arch beats bare",                 &arch, "demo", "demo@arm64", "demo-arch");
    check_facet("no facet matches -> bare",        &miss, "demo", "demo",       "demo-bare");
    check_facet("NULL facets -> bare",             NULL,  "demo", "demo",       "demo-bare");

    /* A gap in the middle must not stop a lower-ranked facet from winning:
     * archonly has no release/version row at all. */
    check_facet("arch row wins with no release/version row", &all, "archonly",
                "archonly@arm64", "Arch.Only");
}

/* --- other well-formed shapes ---------------------------------------------- */

static void test_row_shapes(void) {
    osr_winpkg_spec spec;

    osr_t_eq_int("bucket: bucketed resolves",
                 osr_winpkg_lookup(FIXTURE, "bucketed", NULL, &spec), OSR_WINPKG_OK);
    osr_t_eq_str("bucket: id keeps the bucket prefix", spec.id, "extras/bucketed-app");
    osr_t_eq_str("bucket: bucket extracted", spec.bucket, "extras");

    osr_t_eq_int("comment: commented resolves",
                 osr_winpkg_lookup(FIXTURE, "commented", NULL, &spec), OSR_WINPKG_OK);
    osr_t_eq_str("comment: inline comment stripped from id", spec.id, "Some.Id");

    osr_t_eq_int("duplicate: resolves",
                 osr_winpkg_lookup(FIXTURE, "dup", NULL, &spec), OSR_WINPKG_OK);
    osr_t_eq_str("duplicate: first row of equal specificity wins", spec.id, "first");
}

static void test_non_manager_providers(void) {
    osr_winpkg_spec spec;

    osr_t_eq_int("source: row resolves",
                 osr_winpkg_lookup(FIXTURE, "srcrow", NULL, &spec), OSR_WINPKG_OK);
    osr_t_eq_int("source: provider recognised", spec.provider, OSR_WINPKG_PROV_SOURCE);
    osr_t_eq_str("source: builder name captured", spec.id, "provide_thing");

    osr_t_eq_int("script: row resolves",
                 osr_winpkg_lookup(FIXTURE, "scriptrow", NULL, &spec), OSR_WINPKG_OK);
    osr_t_eq_int("script: provider recognised", spec.provider, OSR_WINPKG_PROV_SCRIPT);
    osr_t_eq_str("script: URL captured whole, its own colon and all",
                 spec.id, "https://example.invalid/install.ps1");

    /* The bucket rule is scoop's alone: a URL is full of slashes and none of
     * them name a bucket. */
    osr_t_eq_int("script: URL with many slashes resolves",
                 osr_winpkg_lookup(FIXTURE, "scriptgh", NULL, &spec), OSR_WINPKG_OK);
    osr_t_eq_str("script: no bucket is inferred from a URL", spec.bucket, "");
    osr_t_eq_str("script: URL survives intact",
                 spec.id, "https://raw.example.invalid/o/r/main/install.ps1");
}

/* The prescribed way to say "a different provider on this architecture":
 * two rows, one provider each, the qualified one replacing the bare one. */
static void test_provider_differs_by_facet(void) {
    osr_winpkg_facets arm = facets_of("", "", "arm64");
    osr_winpkg_spec spec;

    osr_t_eq_int("split: x86_64 gets the manager row",
                 osr_winpkg_lookup(FIXTURE, "split", NULL, &spec), OSR_WINPKG_OK);
    osr_t_eq_str("split: manager id", spec.id, "Vendor.Thing");
    osr_t_eq_int("split: bare row is a manager row", spec.provider, OSR_WINPKG_PROV_WINGET);

    osr_t_eq_int("split: arm64 gets the bin row",
                 osr_winpkg_lookup(FIXTURE, "split", &arm, &spec), OSR_WINPKG_OK);
    osr_t_eq_str("split: arm64 row wins", spec.key, "split@arm64");
    osr_t_eq_int("split: arm64 row is a source row", spec.provider, OSR_WINPKG_PROV_SOURCE);
    osr_t_eq_str("split: arm64 builder", spec.id, "provide_thing");
    osr_t_eq_str("split: the replaced row's bucket does not leak through", spec.bucket, "");
}

/* --- malformed rows are errors, not fallbacks ------------------------------ */

static void check_bad(const char *name) {
    osr_winpkg_spec spec;
    char label[128];
    sprintf(label, "bad row: %s rejected", name);
    osr_t_eq_int(label, osr_winpkg_lookup(FIXTURE, name, NULL, &spec), OSR_WINPKG_BAD_ROW);
}

static void test_bad_rows(void) {
    /* The three that matter most: any row with more than one provider is the
     * old fallback-chain format and must never silently resolve to its first
     * token. A manager paired with a bin: route is that same thing wearing a
     * different hat, so it is rejected exactly as hard. */
    check_bad("multi");
    check_bad("mgrplussource");
    check_bad("twosources");

    check_bad("nocolon");
    check_bad("emptyid");
    check_bad("emptysource");
    check_bad("unknownprov");
    check_bad("norhs");
    check_bad("badbucket");
}

/* --- absent rows and files -------------------------------------------------- */

static void test_missing(void) {
    osr_winpkg_spec spec;

    osr_t_eq_int("missing: unknown name",
                 osr_winpkg_lookup(MAP_PATH, "definitely-not-a-real-package", NULL, &spec),
                 OSR_WINPKG_NOT_FOUND);
    osr_t_true("missing: unknown name leaves spec empty",
               spec.provider == OSR_WINPKG_PROV_NONE && spec.id[0] == '\0');

    osr_t_eq_int("missing: unreadable map file",
                 osr_winpkg_lookup("no/such/file.map", "wezterm", NULL, &spec),
                 OSR_WINPKG_NOT_FOUND);

    /* A facet-qualified row must not answer for a name that has no bare row:
     * `demo` exists in the fixture, `nosuch` does not, under any facet. */
    osr_t_eq_int("missing: facets do not invent a row",
                 osr_winpkg_lookup(FIXTURE, "nosuch", NULL, &spec),
                 OSR_WINPKG_NOT_FOUND);
}

int main(void) {
    OSR_T_INIT();
    test_real_map();
    test_real_map_builders_exist();
    test_real_map_one_provider();
    test_real_map_arch();
    test_facet_precedence();
    test_row_shapes();
    test_non_manager_providers();
    test_provider_differs_by_facet();
    test_bad_rows();
    test_missing();
    return osr_t_finish();
}
