/* test/unit_c/artifact_test.c -- lib/build.c's asset selection.
 *
 * This is the half of the vendor-binary route that decides WHICH file gets
 * downloaded and run, so it is worth pinning down precisely: a pattern that
 * matches too loosely would pick a checksum file, a debug symbol bundle, or
 * another platform's build. Every function here is pure, so all of it runs on
 * any host with no network -- which is why these four live above lib/build.c's
 * platform split rather than inside the body that calls them.
 */
#include "../c_test.h"
#include "../../lib/build.h"

static void test_match_literals(void) {
    osr_t_true("match: identical strings", osr_glob_match("a.zip", "a.zip"));
    osr_t_true("match: different strings do not", !osr_glob_match("a.zip", "b.zip"));
    osr_t_true("match: is anchored at the end",
               !osr_glob_match("posh-windows-amd64.exe", "posh-windows-amd64.exe.sha256"));
    osr_t_true("match: is anchored at the start",
               !osr_glob_match("windows-amd64.zip", "fastfetch-windows-amd64.zip"));
    osr_t_true("match: is case sensitive", !osr_glob_match("A.zip", "a.zip"));
}

static void test_match_star(void) {
    /* The real reason `*` exists: these two projects put the version in the
     * asset filename, so nothing but a glob can name them. */
    osr_t_true("match: PowerShell asset",
               osr_glob_match("PowerShell-*-win-x64.zip", "PowerShell-7.6.4-win-x64.zip"));
    osr_t_true("match: WezTerm asset",
               osr_glob_match("WezTerm-windows-*.zip",
                                "WezTerm-windows-20240203-110809-5046fc22.zip"));

    /* ...and what it must still refuse. */
    osr_t_true("match: x64 pattern rejects the arm64 asset",
               !osr_glob_match("PowerShell-*-win-x64.zip", "PowerShell-7.6.4-win-arm64.zip"));
    osr_t_true("match: zip pattern rejects the msi",
               !osr_glob_match("PowerShell-*-win-x64.zip", "PowerShell-7.6.4-win-x64.msi"));
    osr_t_true("match: zip pattern rejects the checksum",
               !osr_glob_match("WezTerm-windows-*.zip",
                                 "WezTerm-windows-20240203-110809-5046fc22.zip.sha256"));
    osr_t_true("match: * does not span a required literal tail",
               !osr_glob_match("PowerShell-*-win-x64.zip", "PowerShell-7.6.4-win-fxdependent.zip"));

    osr_t_true("match: leading star", osr_glob_match("*.zip", "anything.zip"));
    osr_t_true("match: trailing star", osr_glob_match("starship-*", "starship-x86_64.zip"));
    osr_t_true("match: bare star matches everything", osr_glob_match("*", "whatever"));
    osr_t_true("match: consecutive stars behave",
               osr_glob_match("a**b", "axxxb"));
    /* Backtracking: the first candidate for `*` is wrong and it must retry. */
    osr_t_true("match: backtracks past a false start",
               osr_glob_match("*-win-x64.zip", "PowerShell-7.6.4-win-preview-win-x64.zip"));
}

/* A trimmed but structurally real GitHub /releases/latest payload: the assets
 * are in upstream's own order, with the decoys (checksums, other platforms,
 * other architectures) that make the ordering matter. Joined at runtime
 * because C90 only guarantees 509 characters in one string literal.
 */
#define REL_BASE "https://github.com/PowerShell/PowerShell/releases/download/v7.6.4/"

static char RELEASE_JSON[2048];

static void build_release_json(void) {
    static const char *parts[] = {
        "{\n  \"tag_name\": \"v7.6.4\",\n  \"assets\": [\n",
        "    { \"name\": \"PowerShell-7.6.4-linux-x64.tar.gz\",\n"
        "      \"browser_download_url\": \"" REL_BASE "PowerShell-7.6.4-linux-x64.tar.gz\" },\n",
        "    { \"name\": \"PowerShell-7.6.4-win-arm64.zip\",\n"
        "      \"browser_download_url\": \"" REL_BASE "PowerShell-7.6.4-win-arm64.zip\" },\n",
        "    { \"name\": \"PowerShell-7.6.4-win-x64.msi\",\n"
        "      \"browser_download_url\": \"" REL_BASE "PowerShell-7.6.4-win-x64.msi\" },\n",
        "    { \"name\": \"PowerShell-7.6.4-win-x64.zip\",\n"
        "      \"browser_download_url\": \"" REL_BASE "PowerShell-7.6.4-win-x64.zip\" },\n",
        "    { \"name\": \"PowerShell-7.6.4-win-x64.zip.sha256\",\n"
        "      \"browser_download_url\": \"" REL_BASE "PowerShell-7.6.4-win-x64.zip.sha256\" }\n",
        "  ]\n}\n"
    };
    unsigned long i;

    RELEASE_JSON[0] = '\0';
    for (i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) strcat(RELEASE_JSON, parts[i]);
}

static void test_pick_asset(void) {
    char url[600];

    build_release_json();

    osr_t_true("pick: finds the x64 zip",
               osr_pick_release_asset(RELEASE_JSON, "PowerShell-*-win-x64.zip", url, sizeof(url)));
    osr_t_eq_str("pick: picked the right URL", url,
                 "https://github.com/PowerShell/PowerShell/releases/download/v7.6.4/PowerShell-7.6.4-win-x64.zip");

    osr_t_true("pick: finds the arm64 zip",
               osr_pick_release_asset(RELEASE_JSON, "PowerShell-*-win-arm64.zip", url, sizeof(url)));
    osr_t_eq_str("pick: arm64 URL", url,
                 "https://github.com/PowerShell/PowerShell/releases/download/v7.6.4/PowerShell-7.6.4-win-arm64.zip");

    osr_t_true("pick: no match returns 0",
               !osr_pick_release_asset(RELEASE_JSON, "*-freebsd-*.zip", url, sizeof(url)));
    osr_t_eq_str("pick: no match leaves the output empty", url, "");

    osr_t_true("pick: empty payload returns 0",
               !osr_pick_release_asset("", "*.zip", url, sizeof(url)));
    osr_t_true("pick: payload with no assets returns 0",
               !osr_pick_release_asset("{\"message\":\"Not Found\"}", "*.zip", url, sizeof(url)));

    /* The pattern is matched against the asset's filename, never the whole
     * URL -- otherwise a pattern could be satisfied by the repo or tag name
     * appearing earlier in the path. */
    osr_t_true("pick: pattern does not match against the URL path",
               !osr_pick_release_asset(RELEASE_JSON, "*PowerShell/releases*", url, sizeof(url)));
}

/* --- spec parsing: kind, installer switches, and what remains ------------- */

static void check_spec(const char *label, const char *spec, osr_artifact_kind expect_kind,
                       const char *expect_args, const char *expect_source) {
    osr_artifact_kind kind;
    char args[200];
    char source[600];
    char buf[160];

    sprintf(buf, "spec: %s (parses)", label);
    osr_t_true(buf, osr_parse_artifact_spec(spec, &kind, args, sizeof(args),
                                          source, sizeof(source)));

    sprintf(buf, "spec: %s (kind)", label);
    osr_t_eq_int(buf, kind, expect_kind);

    sprintf(buf, "spec: %s (args)", label);
    osr_t_eq_str(buf, args, expect_args);

    sprintf(buf, "spec: %s (source)", label);
    osr_t_eq_str(buf, source, expect_source);
}

static void test_parse_spec(void) {
    osr_artifact_kind kind;
    char args[200];
    char source[600];

    /* The common case: no kind written, and the "https" before the colon
     * must not be mistaken for one. */
    check_spec("bare URL", "https://h/f.zip", OSR_ARTIFACT_AUTO, "", "https://h/f.zip");
    check_spec("bare gh spec", "gh:o/r:f-*.zip", OSR_ARTIFACT_AUTO, "", "gh:o/r:f-*.zip");

    check_spec("explicit zip", "zip:https://h/download", OSR_ARTIFACT_ZIP, "",
               "https://h/download");
    check_spec("explicit exe", "exe:https://h/tool", OSR_ARTIFACT_EXE, "", "https://h/tool");
    check_spec("explicit msi", "msi:https://h/f.msi", OSR_ARTIFACT_MSI, "", "https://h/f.msi");
    check_spec("kind in front of a gh spec", "msi:gh:o/r:f-*.msi", OSR_ARTIFACT_MSI, "",
               "gh:o/r:f-*.msi");

    /* setup carries the silent switches, because every installer toolkit
     * spells them differently and osr cannot guess. */
    check_spec("setup with one switch", "setup,/S:https://h/setup.exe",
               OSR_ARTIFACT_SETUP, "/S", "https://h/setup.exe");
    check_spec("setup with several switches", "setup,/VERYSILENT,/NORESTART:https://h/s.exe",
               OSR_ARTIFACT_SETUP, "/VERYSILENT /NORESTART", "https://h/s.exe");
    check_spec("setup with no switches", "setup:https://h/s.exe",
               OSR_ARTIFACT_SETUP, "", "https://h/s.exe");

    osr_t_true("spec: empty is rejected",
               !osr_parse_artifact_spec("", &kind, args, sizeof(args), source, sizeof(source)));
    osr_t_true("spec: a kind with no source is rejected",
               !osr_parse_artifact_spec("zip:", &kind, args, sizeof(args), source, sizeof(source)));
}

static void test_kind_of_file(void) {
    osr_t_eq_int("kind: .zip", osr_artifact_kind_of_file("a.zip"), OSR_ARTIFACT_ZIP);
    osr_t_eq_int("kind: .msi", osr_artifact_kind_of_file("PowerShell-7.6.4-win-arm64.msi"),
                 OSR_ARTIFACT_MSI);
    osr_t_eq_int("kind: .exe is the program, not an installer",
                 osr_artifact_kind_of_file("posh-windows-amd64.exe"), OSR_ARTIFACT_EXE);
    osr_t_eq_int("kind: uppercase extension still recognised",
                 osr_artifact_kind_of_file("TOOL.ZIP"), OSR_ARTIFACT_ZIP);
    osr_t_eq_int("kind: unknown extension is AUTO (caller must be told)",
                 osr_artifact_kind_of_file("release-notes.txt"), OSR_ARTIFACT_AUTO);
    osr_t_eq_int("kind: no extension is AUTO",
                 osr_artifact_kind_of_file("download"), OSR_ARTIFACT_AUTO);
}

int main(void) {
    OSR_T_INIT();
    test_match_literals();
    test_match_star();
    test_pick_asset();
    test_parse_spec();
    test_kind_of_file();
    return osr_t_finish();
}
