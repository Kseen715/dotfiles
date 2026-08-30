/* modules/mirrors.c -- rank the distro's package mirrors by speed. It was a
 * script you ran by hand before the installer; as a module it is
 * `osr module mirrors`, or a first line in a rice.list.
 *
 * Deliberately NOT in arch-hyprland-glass/rice.list: ranking probes every mirror
 * in the list and takes minutes, which is the wrong thing to do implicitly at
 * the top of every install. Opt in when a box actually has slow mirrors.
 *
 * Rerun-safe (§2) by stamp file, not by re-ranking: the ranking is a snapshot
 * that ages, so redoing it is `OSR_MIRRORS_FORCE=1 osr module mirrors` (or
 * deleting the stamp), never a silent multi-minute step on a second run.
 *
 * Only pacman has a first-class ranker in-tree (rankmirrors, from pacman-contrib)
 * and only dnf has a built-in fastest-mirror selector. apt/apk/xbps/portage rank
 * via out-of-tree tooling or a CDN that already does it, so they log and no-op
 * rather than pretending (§9: degrade, never fake it).
 *
 * Was modules/mirrors.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/fetch.h"

#include <stddef.h>
#include <stdio.h>
#include <unistd.h>

#define ALL_URL "https://archlinux.org/mirrorlist/all/"

/* The system files this writes. They are literals with an override a test can
 * set to sandbox them -- the same trick lib/user.c uses for /etc/passwd and
 * modules/swap.c for /proc/meminfo. A module that writes /etc by absolute path
 * has no other way to be exercised without root (§5a). */
static const char *pacman_dir(void) { return env_str("OSR_PACMAN_DIR", "/etc/pacman.d"); }
static const char *dnf_conf(void)   { return env_str("OSR_DNF_CONF", "/etc/dnf/dnf.conf"); }

/* pac_path -- <pacman dir>/<name>, into a caller-owned Str. */
static void pac_path(Str *out, const char *name) {
    str_reset(out);
    str_addz(out, pacman_dir());
    str_addc(out, '/');
    str_addz(out, name);
}

/* tmp_path -- the sh module's `${TMPDIR:-/tmp}/<name>.$$`. */
static void tmp_path(Str *out, const char *name) {
    str_addz(out, env_str("TMPDIR", "/tmp"));
    str_addc(out, '/');
    str_addz(out, name);
    str_addc(out, '.');
    str_addl(out, (long)getpid());
}

/* has_server_line -- `grep -q '^[[:space:]]*Server' <file>`. */
static int has_server_line(const char *path) {
    char *buf;
    size_t len, pos = 0;
    Line line;
    int found = 0;

    buf = slurp(path, &len);
    if (buf == NULL) return 0;
    while (!found && next_line(buf, len, &pos, &line)) {
        size_t i = 0;
        while (i < line.len && is_space(line.start[i])) i++;
        if (line.len - i >= 6 && strncmp(line.start + i, "Server", 6) == 0) found = 1;
    }
    free(buf);
    return found;
}

/* file_nonempty -- `[ -s <path> ]`. */
static int file_nonempty(const char *path) {
    size_t len;
    char *buf = slurp(path, &len);
    if (buf == NULL) return 0;
    free(buf);
    return len > 0;
}

/* uncomment_servers -- the published all-mirrors list ships every Server line
 * commented out; this is the sh module's sed that turns them back on. */
static void uncomment_servers(Str *out, const char *path) {
    char *buf;
    size_t len, pos = 0;
    Line line;

    buf = slurp(path, &len);
    if (buf == NULL) return;
    while (next_line(buf, len, &pos, &line)) {
        size_t i = 0;
        while (i < line.len && is_space(line.start[i])) i++;
        if (i < line.len && line.start[i] == '#') {
            size_t j = i + 1;
            while (j < line.len && is_space(line.start[j])) j++;
            if (line.len - j >= 6 && strncmp(line.start + j, "Server", 6) == 0) {
                str_add(out, line.start + j, line.len - j);
                str_addc(out, '\n');
                continue;
            }
        }
        str_add(out, line.start, line.len);
        str_addc(out, '\n');
    }
    free(buf);
}

/* has_fastestmirror -- `grep -qE '^[[:space:]]*fastestmirror[[:space:]]*='`. */
static int has_fastestmirror(const char *path) {
    char *buf;
    size_t len, pos = 0;
    Line line;
    int found = 0;

    buf = slurp(path, &len);
    if (buf == NULL) return 0;
    while (!found && next_line(buf, len, &pos, &line)) {
        size_t i = 0;
        while (i < line.len && is_space(line.start[i])) i++;
        if (line.len - i < 14) continue;
        if (strncmp(line.start + i, "fastestmirror", 13) != 0) continue;
        i += 13;
        while (i < line.len && is_space(line.start[i])) i++;
        if (i < line.len && line.start[i] == '=') found = 1;
    }
    free(buf);
    return found;
}

/* rank_pacman -- rank from the pristine backup, never from an already truncated
 * mirrorlist (re-ranking a 16-entry list would just re-confirm whichever mirror
 * won the first time). Writes to a temp file and validates it before installing,
 * so a failed or empty rankmirrors can never leave the box with no mirrors at
 * all -- the worst case is the current list, untouched.
 *
 * Never fatal: a slow/unreachable mirror probe must not kill an install that the
 * existing mirrorlist can serve perfectly well. Success is signalled by the
 * stamp file, which the caller reads (this runs in a forked step, so a variable
 * could not carry the answer back - the filesystem can). */
static int rank_pacman(void *ctx) {
    Str out, ranked, backup, live, stamp;
    char *argv[5];
    FILE *f;

    (void)ctx;
    str_init(&out); str_init(&ranked);
    str_init(&backup); str_init(&live); str_init(&stamp);
    pac_path(&backup, "mirrorlist.backup");
    pac_path(&live, "mirrorlist");
    pac_path(&stamp, ".osr-mirrors-ranked");
    tmp_path(&out, "osr-mirrorlist");

    /* `rankmirrors ... >"$_rp_out" 2>/dev/null || :` -- captured and then
     * written, because it is the FILE the validation greps and the file `cp`
     * installs, and a failed run has to leave an empty one rather than none. */
    argv[0] = (char *)"rankmirrors"; argv[1] = (char *)"-n";
    argv[2] = (char *)env_str("OSR_MIRRORS_N", "16");
    argv[3] = backup.p; argv[4] = NULL;
    (void)osr_run_capture(argv, &ranked);
    f = fopen(str_text(&out), "wb");
    if (f != NULL) {
        if (ranked.len > 0) (void)fwrite(str_text(&ranked), 1, ranked.len, f);
        fclose(f);
    }
    str_free(&ranked);

    if (has_server_line(str_text(&out))) {
        char *cp[5];
        char *touch[3];
        cp[0] = (char *)"cp"; cp[1] = (char *)"-f"; cp[2] = out.p;
        cp[3] = live.p; cp[4] = NULL;
        (void)osr_run_root(cp);
        touch[0] = (char *)"touch"; touch[1] = stamp.p; touch[2] = NULL;
        (void)osr_run_root(touch);
    }
    (void)unlink(str_text(&out));
    str_free(&out); str_free(&backup); str_free(&live); str_free(&stamp);
    return 1;
}

int osrm_mirrors(void) {
    static const char *const contrib[] = { "pacman-contrib", NULL };
    const char *mgr = osr_mod_pkg();
    const char *n = env_str("OSR_MIRRORS_N", "16");
    int ok = 1;

    if (strcmp(mgr, "pacman") == 0) {
        Str desc, live, backup, stamp;

        str_init(&live); str_init(&backup); str_init(&stamp);
        pac_path(&live, "mirrorlist");
        pac_path(&backup, "mirrorlist.backup");
        pac_path(&stamp, ".osr-mirrors-ranked");

        if (file_exists(str_text(&stamp)) && !env_is_set("OSR_MIRRORS_FORCE")) {
            str_free(&live); str_free(&backup); str_free(&stamp);
            osr_info("mirrors already ranked - skipping (OSR_MIRRORS_FORCE=1 to redo)");
            return 1;
        }

        /* No mirrorlist at all (rare, but a broken/emptied /etc/pacman.d is how
         * people get here): fetch the full list and uncomment it, because the
         * published file ships every Server line commented out. This runs BEFORE
         * pkg_install -- without mirrors pacman cannot install the ranker
         * either. */
        if (!file_nonempty(str_text(&live))) {
            Str tmp;
            osr_warn("no /etc/pacman.d/mirrorlist - fetching the full Arch mirror list");
            str_init(&tmp);
            tmp_path(&tmp, "osr-mirrorlist-all");
            if (osr_fetch_download(ALL_URL, str_text(&tmp), 0)) {
                Str body;
                char *argv[4];
                argv[0] = (char *)"mkdir"; argv[1] = (char *)"-p";
                argv[2] = (char *)pacman_dir(); argv[3] = NULL;
                (void)osr_run_root(argv);
                str_init(&body);
                uncomment_servers(&body, str_text(&tmp));
                (void)osr_write_root(str_text(&live), str_text(&body));
                str_free(&body);
                (void)unlink(str_text(&tmp));
            } else {
                (void)unlink(str_text(&tmp));
                str_free(&tmp);
                osr_die("could not fetch a mirrorlist and none exists - fix "
                        "/etc/pacman.d/mirrorlist first");
            }
            str_free(&tmp);
        }

        /* One pristine backup, kept forever: it is both the safety net and the
         * input every future ranking reads. */
        if (!file_exists(str_text(&backup))) {
            char *argv[5];
            argv[0] = (char *)"cp"; argv[1] = (char *)"-f"; argv[2] = live.p;
            argv[3] = backup.p; argv[4] = NULL;
            (void)osr_run_root(argv);
            osr_info("backed up mirrorlist to /etc/pacman.d/mirrorlist.backup");
        }

        ok = osr_pkg_install_step("Installing mirror-ranking tools", contrib) && ok;
        str_init(&desc);
        str_addz(&desc, "Ranking the "); str_addz(&desc, n);
        str_addz(&desc, " fastest mirrors");
        ok = osr_step(str_text(&desc), rank_pacman, NULL) && ok;
        str_free(&desc);

        if (file_exists(str_text(&stamp))) {
            /* The index now points at different mirrors - refresh it, and let
             * the rest of the run reuse that refresh (§2). */
            osr_pkg_refresh();
        } else {
            osr_warn("rankmirrors produced no usable list - keeping the existing mirrorlist");
        }
        str_free(&live); str_free(&backup); str_free(&stamp);
    } else if (strcmp(mgr, "dnf") == 0) {
        /* dnf ranks on its own; the config just has to ask it to. Appended once
         * (a user-set value is left alone, G2). */
        if (has_fastestmirror(dnf_conf())) {
            osr_info("dnf fastestmirror already configured - skipping");
        } else {
            osr_info("enabling dnf fastestmirror + parallel downloads");
            (void)osr_append_root(dnf_conf(),
                                  "fastestmirror=True\nmax_parallel_downloads=10\n");
        }
    } else {
        osr_infof("no in-tree mirror ranker for '%s' - skipping", mgr);
    }
    return ok;
}
