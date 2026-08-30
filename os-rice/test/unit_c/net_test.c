/* test/unit_c/net_test.c -- what lib/fetch.c must do: pick a downloader, run
 * it with the right flags, report progress, and read a tag out of GitHub.
 *
 * Everything os-rice installs that a package manager does not carry comes
 * through here, so the flags are not decoration: `-f` is what turns a 404 into
 * a failure instead of an HTML file named like a tarball, and `-L` is what
 * makes a GitHub release URL resolve to the CDN it redirects to.
 *
 * Hermetic: $PATH is a directory of stubs, so "does this box have curl" is a
 * property of the scenario and every response is text this test wrote. Nothing
 * here touches the network.
 *
 * ON THE PROGRESS READOUT
 *
 * It is asserted as SHAPE rather than as exact bytes, because the numbers
 * depend on how far a download got before a poll fired -- a race this test has
 * no business pinning. What does not depend on timing, and is asserted: that
 * the lines are newline-terminated rather than \\r-redrawn (the live step
 * window can only render whole lines), that the filename is what gets cropped
 * on a narrow terminal rather than the numbers, that the readout is silent
 * below the size threshold, and that the fetch's exit status survives the
 * wrapper around it.
 *
 * Replaces test/unit/net_c_parity.sh and download_progress.sh, which drove
 * lib/net.sh. The pure header/JSON parsing is asserted separately and directly
 * in net_parse_test.c. See test/harness.h.
 */
#include "../harness.c"

static OsrSandbox sb;

/* net -- `osr net <args>`, log cleared first. */
static int net1(const char *a, const char *b) {
    osr_sb_reset(&sb);
    return osr_sb_run_core(&sb, "net", a, b, (const char *)NULL);
}
static int net2(const char *a, const char *b, const char *c, const char *d) {
    osr_sb_reset(&sb);
    return osr_sb_run_core(&sb, "net", a, b, c, d, (const char *)NULL);
}

/* backend_is -- which downloader `osr net backend` names. */
static void backend_is(const char *expected, const char *label) {
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "net", "backend", (const char *)NULL);
    osr_assert_out_is(&sb, expected, label);
}

/* curl_stub -- the downloader, with `extra` appended for the scenario's own
 * behaviour (writing a payload, answering a HEAD, failing). */
static void curl_stub(const char *extra) {
    HStr body;
    hs_init(&body);
    hs_add(&body, "printf 'curl %s\\n' \"$*\" >>\"$LOG\"\n");
    hs_add(&body, extra);
    osr_sb_stub_body(&sb, "curl", hs_text(&body));
    hs_free(&body);
}

/* longest_line -- the width of the widest line in `text`. How the test asks
 * whether anything overflowed the terminal. */
static int longest_line(const char *text) {
    int best = 0, cur = 0;
    for (; *text != '\0'; text++) {
        if (*text == '\n') { if (cur > best) best = cur; cur = 0; }
        else cur++;
    }
    if (cur > best) best = cur;
    return best;
}

/* count_lines -- how many non-empty lines `text` holds. */
static int count_lines(const char *text) {
    int n = 0;
    const char *p = text;
    while (*p != '\0') {
        const char *e = strchr(p, '\n');
        size_t len = e != NULL ? (size_t)(e - p) : strlen(p);
        if (len > 0) n++;
        if (e == NULL) break;
        p = e + 1;
    }
    return n;
}

int main(void) {
    HStr dest;

    osr_sb_init(&sb);
    hs_init(&dest);
    osr_sb_env(&sb, "COLUMNS", "80");

    /* ================================================================
     * 1. Which downloader
     *
     * curl, then wget, then a busybox that actually carries the applet. The
     * order is not a preference: curl is the only one of the three whose
     * flags every builder in lib/build.c is written against, and busybox is
     * the last resort because a fresh Alpine has nothing else.
     * ================================================================ */
    curl_stub("");
    osr_sb_stub_body(&sb, "wget", "printf 'wget %s\\n' \"$*\" >>\"$LOG\"\n");
    backend_is("curl\n", "curl is chosen when the box has it");

    osr_sb_rm(&sb, "bin/curl");
    backend_is("wget\n", "wget is the fallback when curl is missing");

    osr_sb_rm(&sb, "bin/wget");
    osr_sb_stub_body(&sb, "busybox",
        "printf 'busybox %s\\n' \"$*\" >>\"$LOG\"\n"
        "[ \"$1\" = wget ] || exit 1\nexit 0\n");
    backend_is("busybox-wget\n", "a busybox carrying the wget applet is the last resort");

    /* A busybox is not a downloader just by being present -- the build may
     * have been compiled without the applet, and finding that out at fetch
     * time rather than at probe time is a failure in the middle of an install. */
    osr_sb_stub_body(&sb, "busybox", "exit 1\n");
    backend_is("\n", "a busybox WITHOUT the wget applet is not a downloader");
    osr_sb_rm(&sb, "bin/busybox");

    /* ================================================================
     * 2. The fetch command itself -- the flags ARE the port
     * ================================================================ */
    curl_stub("printf 'PAYLOAD\\n'\n");
    net1("fetch", "https://example.invalid/x");
    osr_assert_log_is(&sb,
        "curl -fsSL https://example.invalid/x\n",
        "fetch: -f fails on a 404 instead of saving the error page, -L follows "
        "the redirect, -sS stays quiet but still reports a real error");
    osr_assert_out_is(&sb, "PAYLOAD\n", "fetch: the body reaches stdout unaltered");

    /* A download below the progress threshold takes the silent path: one
     * curl -o and not a word. */
    osr_sb_env(&sb, "OSR_PROGRESS_MIN_BYTES", "1048576");
    curl_stub(
        "_next=; _dest=\n"
        "for a in \"$@\"; do\n"
        "  [ \"$_next\" = dest ] && { _dest=$a; _next=; }\n"
        "  [ \"$a\" = \"-o\" ] && _next=dest\n"
        "done\n"
        "[ -n \"$_dest\" ] && printf 'PAYLOAD\\n' >\"$_dest\"\n"
        "exit 0\n");
    hs_path(&dest, hs_text(&sb.root), "small.tar");
    net2("download", "https://example.invalid/small.tar", hs_text(&dest), "1024");
    osr_assert_log_is(&sb,
        "curl -fsSL -o ROOT/small.tar https://example.invalid/small.tar\n",
        "download: one curl -o, and no HEAD probe when the caller gave a size");
    osr_assert_silent(&sb, "download: a file under the threshold prints nothing");

    /* ================================================================
     * 3. The progress readout
     *
     * A meter exists because a 1 GiB download behind a silent `curl -sS` is
     * indistinguishable from a hung install. What it must not do is redraw
     * with \\r: a module's stdout is the live step window's log FILE, where a
     * carriage return is just a byte in the middle of a line nobody can read.
     * ================================================================ */
    osr_sb_env(&sb, "OSR_PROGRESS_MIN_BYTES", "1024");
    osr_sb_env(&sb, "OSR_DOWNLOAD_POLL", "1");
    osr_sb_real(&sb, "dd");
    curl_stub(
        "_next=; _dest=\n"
        "for a in \"$@\"; do\n"
        "  [ \"$_next\" = dest ] && { _dest=$a; _next=; }\n"
        "  [ \"$a\" = \"-o\" ] && _next=dest\n"
        "done\n"
        "[ -n \"$_dest\" ] && dd if=/dev/zero of=\"$_dest\" bs=1024 count=4096 2>/dev/null\n"
        "exit 0\n");
    hs_path(&dest, hs_text(&sb.root), "big.tar");
    net2("download", "https://example.invalid/big-release-archive.tar.gz",
         hs_text(&dest), "4194304");
    osr_assert_out(&sb, "big-release-archive.tar.gz",
        "progress: every line names the file being fetched");
    osr_assert_out(&sb, "MiB / 4 MiB",
        "progress: the readout gives the current size AND the total");
    osr_assert_out(&sb, "MiB (100%)",
        "progress: the finished size is reported, so a done download says so");
    osr_assert_true(strchr(osr_sb_capture(&sb), '\r') == NULL,
        "progress: the lines are newline-terminated, never \\r-redrawn -- a "
        "module's stdout is a log file, not a terminal");
    osr_assert_true(count_lines(osr_sb_capture(&sb)) >= 1,
        "progress: the readout arrives as whole lines");

    /* On a narrow terminal something has to give, and it is the NAME: the
     * numbers are the reason the line exists. */
    osr_sb_env(&sb, "COLUMNS", "40");
    hs_path(&dest, hs_text(&sb.root), "long.tar");
    net2("download",
         "https://example.invalid/a-very-long-release-asset-name-that-will-not-fit.tar.gz",
         hs_text(&dest), "4194304");
    osr_assert_out(&sb, "...", "progress: an over-long name is cropped with a marker");
    osr_assert_out(&sb, "MiB / 4 MiB", "progress: the byte counts survive the crop");
    osr_assert_true(longest_line(osr_sb_capture(&sb)) <= 40,
        "progress: no line overflows the terminal it was measured against");
    osr_sb_env(&sb, "COLUMNS", "80");

    /* The wrapper must not eat the answer: a download that failed has to fail,
     * or a builder unpacks a file that was never written. */
    curl_stub("exit 22\n");
    hs_path(&dest, hs_text(&sb.root), "gone.tar");
    osr_assert_rc(net2("download", "https://example.invalid/gone.tar",
                       hs_text(&dest), "4194304"), 1,
        "download: the fetch's failure survives the progress wrapper");

    /* No size from the caller: a Content-Length probe supplies one, so a
     * builder that does not know how big its asset is still gets a meter. */
    curl_stub(
        "case \"$*\" in\n"
        "  *-fsSLI*) printf 'HTTP/1.1 200 OK\\r\\nContent-Length: 4194304\\r\\n\\r\\n'; exit 0 ;;\n"
        "esac\n"
        "_next=; _dest=\n"
        "for a in \"$@\"; do\n"
        "  [ \"$_next\" = dest ] && { _dest=$a; _next=; }\n"
        "  [ \"$a\" = \"-o\" ] && _next=dest\n"
        "done\n"
        "[ -n \"$_dest\" ] && dd if=/dev/zero of=\"$_dest\" bs=1024 count=4096 2>/dev/null\n"
        "exit 0\n");
    hs_path(&dest, hs_text(&sb.root), "probed.tar");
    net2("download", "https://example.invalid/probed.tar", hs_text(&dest), NULL);
    osr_assert_out(&sb, "MiB / 4 MiB",
        "download: a call with no size takes its total from a Content-Length probe");
    osr_assert_log(&sb, "curl -fsSLI --max-time 20 https://example.invalid/probed.tar",
        "download: the probe is a HEAD with a timeout, not a second full fetch");

    /* A response that will not say how big it is (chunked) falls back to the
     * silent fetch rather than to a meter counting against nothing. */
    curl_stub(
        "case \"$*\" in\n"
        "  *-fsSLI*) printf 'HTTP/1.1 200 OK\\r\\nTransfer-Encoding: chunked\\r\\n\\r\\n'; exit 0 ;;\n"
        "esac\n"
        "_next=; _dest=\n"
        "for a in \"$@\"; do\n"
        "  [ \"$_next\" = dest ] && { _dest=$a; _next=; }\n"
        "  [ \"$a\" = \"-o\" ] && _next=dest\n"
        "done\n"
        "[ -n \"$_dest\" ] && dd if=/dev/zero of=\"$_dest\" bs=1024 count=64 2>/dev/null\n"
        "exit 0\n");
    hs_path(&dest, hs_text(&sb.root), "chunked.tar");
    osr_assert_rc(net2("download", "https://example.invalid/chunked.tar",
                       hs_text(&dest), NULL), 0,
        "download: an unknown size is not an error");
    osr_assert_silent(&sb,
        "download: an unknown size falls back to the silent fetch, not a meter "
        "counting against nothing");

    /* ================================================================
     * 4. What a HEAD answers
     *
     * Both of these read the LAST value across a redirect chain, because each
     * hop reports its own: the 302 carries a Content-Length of its own tiny
     * body, and taking the first would size every download at a few bytes.
     * ================================================================ */
    curl_stub(
        "case \"$*\" in *-fsSLI*)\n"
        "  printf 'HTTP/1.1 302 Found\\r\\nContent-Length: 12\\r\\n"
        "Location: https://cdn.example.invalid/final/tsetup.7.0.9.tar.xz\\r\\n\\r\\n'\n"
        "  printf 'HTTP/1.1 200 OK\\r\\nContent-Length: 4194304\\r\\n\\r\\n' ;;\n"
        "esac\n");
    net1("size", "https://example.invalid/x");
    osr_assert_out_is(&sb, "4194304\n",
        "size: the LAST Content-Length wins -- a redirect hop reports its own");
    net1("final-url", "https://example.invalid/x");
    osr_assert_out_is(&sb, "https://cdn.example.invalid/final/tsetup.7.0.9.tar.xz\n",
        "final-url: the LAST Location wins, which is the file rather than a hop");

    /* ================================================================
     * 5. github-latest
     *
     * A version pinned in a map row goes stale; asking GitHub does not. The
     * fallback matters more than it looks: plenty of projects tag releases
     * without ever publishing a GitHub Release object, and for those the
     * /releases/latest endpoint answers 404 forever.
     * ================================================================ */
    curl_stub(
        "case \"$*\" in\n"
        "  *releases/latest*) printf '{\"url\":\"x\",\"tag_name\": \"v2.63.0\","
        "\"name\":\"2.63.0\"}\\n' ;;\n"
        "  */tags*) printf '[{\"name\":\"v9.9.9-tag-fallback\"}]\\n' ;;\n"
        "esac\n");
    net1("github-latest", "cli/cli");
    osr_assert_out_is(&sb, "v2.63.0",
        "github-latest: tag_name is the answer, not the release's display name");

    curl_stub(
        "case \"$*\" in\n"
        "  *releases/latest*) printf '{\"message\":\"Not Found\"}\\n' ;;\n"
        "  */tags*) printf '[{\"name\":\"v9.9.9-tag-fallback\"}]\\n' ;;\n"
        "esac\n");
    net1("github-latest", "cli/cli");
    osr_assert_out_is(&sb, "v9.9.9-tag-fallback",
        "github-latest: a repo with no published release falls back to its "
        "first tag rather than failing");

    hs_free(&dest);
    osr_sb_free(&sb);
    return osr_finish();
}
