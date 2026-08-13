/* test/unit_c/winpkg_test.c -- lib/winpkg.c's windows.map lookup.
 * Platform-independent: osr_winpkg_lookup is plain fopen/fgets and takes its
 * facets as a parameter, so the whole resolver runs on any host.
 *
 * Two maps are exercised. The real os-rice/windows.map, so drift between the
 * map and this test never goes unnoticed -- including the invariant that every
 * row in it names exactly one manager. And fixtures/windows_facets.map, for
 * facet precedence and for the malformed rows the real map must never contain.
 */
#include "../c_test.h"
#include "../../lib/winpkg.h"

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

static void check_real_row(const char *name, osr_winpkg_mgr mgr, const char *id) {
    osr_winpkg_spec spec;
    char label[128];

    sprintf(label, "windows.map: %s resolves", name);
    osr_t_eq_int(label, osr_winpkg_lookup(MAP_PATH, name, NULL, &spec), OSR_WINPKG_OK);

    sprintf(label, "windows.map: %s manager", name);
    osr_t_eq_str(label, osr_winpkg_mgr_name(spec.mgr), osr_winpkg_mgr_name(mgr));

    sprintf(label, "windows.map: %s id", name);
    osr_t_eq_str(label, spec.id, id);
}

static void test_real_map(void) {
    osr_winpkg_spec spec;

    /* Every row is pinned to one manager, and to the manager that project's
     * own Windows install page names -- see the header of windows.map. */
    check_real_row("pwsh",       OSR_WINPKG_MGR_WINGET, "Microsoft.PowerShell");
    check_real_row("wezterm",    OSR_WINPKG_MGR_WINGET, "wez.wezterm");
    check_real_row("oh-my-posh", OSR_WINPKG_MGR_WINGET, "JanDeDobbeleer.OhMyPosh");
    check_real_row("starship",   OSR_WINPKG_MGR_WINGET, "Starship.Starship");
    check_real_row("fastfetch",  OSR_WINPKG_MGR_SCOOP,  "fastfetch");

    /* fastfetch is in scoop's main bucket, so it carries no bucket prefix --
     * if that ever changes the install must run `scoop bucket add` first. */
    osr_winpkg_lookup(MAP_PATH, "fastfetch", NULL, &spec);
    osr_t_eq_str("windows.map: fastfetch needs no bucket", spec.bucket, "");
}

/* The one-provider rule, asserted against the real file: every row resolves
 * to a manager OR a bin: spec, never both. A row that grew a second provider
 * would still parse as OK under the old format, so this is worth checking on
 * the real map and not only on the fixture.
 */
static void test_real_map_one_provider(void) {
    static const char *names[5] = { "pwsh", "wezterm", "oh-my-posh", "starship", "fastfetch" };
    osr_winpkg_spec spec;
    char label[128];
    int i;

    for (i = 0; i < 5; i++) {
        osr_winpkg_lookup(MAP_PATH, names[i], NULL, &spec);

        sprintf(label, "windows.map: %s names exactly one provider", names[i]);
        osr_t_true(label, (spec.mgr != OSR_WINPKG_MGR_NONE) != (spec.bin[0] != '\0'));
    }
}

/* Architecture is the manager's job wherever the manager has the build --
 * verified against winget's and scoop's own manifests, which carry arm64
 * installers for all four of these. An @arm64 row in front of a provider
 * that already resolves the architecture would be a downgrade, so the real
 * map must have none, and every name must fall to its bare row on arm64.
 */
static void test_real_map_arch(void) {
    static const char *names[5] = { "pwsh", "wezterm", "oh-my-posh", "starship", "fastfetch" };
    osr_winpkg_facets arm = facets_of("", "", "arm64");
    osr_winpkg_spec spec;
    char label[128];
    int i;

    for (i = 0; i < 5; i++) {
        osr_t_eq_int("arm64: resolves", osr_winpkg_lookup(MAP_PATH, names[i], &arm, &spec),
                     OSR_WINPKG_OK);

        sprintf(label, "arm64: %s uses its bare row (no arch override)", names[i]);
        osr_t_eq_str(label, spec.key, names[i]);
    }
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

static void test_bin_rows(void) {
    osr_winpkg_spec spec;

    osr_t_eq_int("bin: URL row resolves",
                 osr_winpkg_lookup(FIXTURE, "binurl", NULL, &spec), OSR_WINPKG_OK);
    osr_t_eq_int("bin: a bin row names no manager", spec.mgr, OSR_WINPKG_MGR_NONE);
    osr_t_eq_str("bin: spec captured", spec.bin, "https://example.invalid/only.exe");

    osr_t_eq_int("bin: gh row resolves",
                 osr_winpkg_lookup(FIXTURE, "bingh", NULL, &spec), OSR_WINPKG_OK);
    osr_t_eq_str("bin: gh spec captured whole, colons and all",
                 spec.bin, "gh:owner/repo:tool-*-win64.zip");

    osr_t_eq_int("bin: explicit kind resolves",
                 osr_winpkg_lookup(FIXTURE, "binkind", NULL, &spec), OSR_WINPKG_OK);
    osr_t_eq_str("bin: kind stays part of the spec",
                 spec.bin, "msi:https://example.invalid/tool.msi");

    osr_t_eq_int("bin: setup row with switches resolves",
                 osr_winpkg_lookup(FIXTURE, "binsetup", NULL, &spec), OSR_WINPKG_OK);
    osr_t_eq_str("bin: setup switches survive the map parse",
                 spec.bin, "setup,/S:https://example.invalid/setup.exe");
}

/* The prescribed way to say "a different provider on this architecture":
 * two rows, one provider each, the qualified one replacing the bare one. */
static void test_provider_differs_by_facet(void) {
    osr_winpkg_facets arm = facets_of("", "", "arm64");
    osr_winpkg_spec spec;

    osr_t_eq_int("split: x86_64 gets the manager row",
                 osr_winpkg_lookup(FIXTURE, "split", NULL, &spec), OSR_WINPKG_OK);
    osr_t_eq_str("split: manager id", spec.id, "Vendor.Thing");
    osr_t_eq_str("split: manager row carries no bin spec", spec.bin, "");

    osr_t_eq_int("split: arm64 gets the bin row",
                 osr_winpkg_lookup(FIXTURE, "split", &arm, &spec), OSR_WINPKG_OK);
    osr_t_eq_str("split: arm64 row wins", spec.key, "split@arm64");
    osr_t_eq_int("split: arm64 row has no manager", spec.mgr, OSR_WINPKG_MGR_NONE);
    osr_t_eq_str("split: arm64 artifact", spec.bin, "https://vendor.invalid/thing-arm64.zip");
    osr_t_eq_str("split: the replaced row's id does not leak through", spec.id, "");
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
    check_bad("mgrplusbin");
    check_bad("twobins");

    check_bad("nocolon");
    check_bad("emptyid");
    check_bad("unknownmgr");
    check_bad("norhs");
    check_bad("badbucket");
    check_bad("emptybin");
}

/* --- absent rows and files -------------------------------------------------- */

static void test_missing(void) {
    osr_winpkg_spec spec;

    osr_t_eq_int("missing: unknown name",
                 osr_winpkg_lookup(MAP_PATH, "definitely-not-a-real-package", NULL, &spec),
                 OSR_WINPKG_NOT_FOUND);
    osr_t_true("missing: unknown name leaves spec empty",
               spec.mgr == OSR_WINPKG_MGR_NONE && spec.id[0] == '\0');

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
    test_real_map_one_provider();
    test_real_map_arch();
    test_facet_precedence();
    test_row_shapes();
    test_bin_rows();
    test_provider_differs_by_facet();
    test_bad_rows();
    test_missing();
    return osr_t_finish();
}
