/* lib/pkg.c -- the C port of lib/pkg.sh: name resolution through lib/pkgmap/,
 * the native installer for every package manager, and the provider-tagged
 * install methods (§4).
 *
 * Split out of lib/module.c, where the native half lived while it was the only
 * half. The shape is lib/pkg.sh's own: pkg_install expands each logical name
 * through the map, batches everything native into ONE install command, then
 * dispatches the provider rows in manifest order -- two passes, because the
 * native batch carries the downloaders and toolchains (curl, rust) a provider
 * row may need.
 *
 * Providers implemented here: script: (a piped installer), cargo: (a crate,
 * binstall first), aur: (paru/yay). source: is the one that still goes back to
 * lib/pkg.sh, because its builders are lib/build.sh shell functions; when
 * those are ported that branch goes away and nothing else changes.
 *
 * `osr pkg <verb>` exposes the same verbs as a command, which is what lets
 * test/unit/pkg_c_parity.sh diff this against lib/pkg.sh over a stubbed PATH.
 *
 * C89 + POSIX.
 */
#define _XOPEN_SOURCE 700

#include "common.h"
#include "cmds.h"
#include "module.h"
#include "fetch.h"
#include "build.h"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

/* --- packages ------------------------------------------------------------- */

/* row_rhs -- the value half of a pkgmap row, given the text just past the '=':
 * a trailing ` # comment` dropped (the space before # is required, so `a#b`
 * survives) and both ends trimmed. _pkgmap_rhs in lib/pkg.sh. */
static void row_rhs(Str *out, const char *p, size_t remain) {
    size_t i;
    size_t end = remain;
    for (i = 0; i + 1 < remain; i++) {
        if (is_space(p[i]) && p[i + 1] == '#') { end = i; break; }
    }
    while (end > 0 && is_space(p[end - 1])) end--;
    for (i = 0; i < end && is_space(p[i]); i++) { /* ltrim */ }
    str_add(out, p + i, end - i);
}

/* map_lookup -- one pkgmap row: `^[[:space:]]*<key>[[:space:]]*=`, then the
 * right-hand side per row_rhs. Same two files, same order, as _pkgmap_exact. */
static int map_lookup(Str *out, const char *map_path, const char *key) {
    char *buf;
    size_t len;
    size_t pos = 0;
    Line line;
    int found = 0;

    buf = slurp(map_path, &len);
    if (buf == NULL) return 0;
    while (!found && next_line(buf, len, &pos, &line)) {
        const char *p = line.start;
        size_t remain = line.len;
        size_t klen = strlen(key);
        while (remain > 0 && is_space(*p)) { p++; remain--; }
        if (remain <= klen || strncmp(p, key, klen) != 0) continue;
        p += klen;
        remain -= klen;
        while (remain > 0 && is_space(*p)) { p++; remain--; }
        if (remain == 0 || *p != '=') continue;
        p++;
        remain--;
        row_rhs(out, p, remain);
        found = 1;
    }
    free(buf);
    return found;
}

/* ver_cmp -- _ver_cmp in lib/pkg.sh: component-wise numeric compare, -1/0/1.
 * Missing components count as 0 (3 == 3.0.0, and 3.21 < 3.21.3), and each
 * component keeps its leading digits only, which is what makes 15-SP5,
 * 3.24_alpha and Ubuntu's 24.04 comparable at all. */
static int ver_cmp(const char *a, const char *b) {
    while (*a != '\0' || *b != '\0') {
        long x = 0;
        long y = 0;
        while (*a >= '0' && *a <= '9') x = x * 10 + (*a++ - '0');
        while (*b >= '0' && *b <= '9') y = y * 10 + (*b++ - '0');
        if (x != y) return x > y ? 1 : -1;
        while (*a != '\0' && *a != '.') a++;
        while (*b != '\0' && *b != '.') b++;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
    return 0;
}

/* ver_match -- _ver_match: does <ver> satisfy a comparison facet (`<=3.20`,
 * `<3.22`, `>=0.66`, `>13`)? Anything not starting with an operator is not a
 * range, so a plain `name@3.20` key is never mistaken for one. */
static int ver_match(const char *ver, const char *expr) {
    int op;          /* 0 '<'   1 "<="   2 '>'   3 ">=" */
    int c;

    if (expr[0] == '<') {
        op = expr[1] == '=' ? 1 : 0;
        expr += op == 1 ? 2 : 1;
    } else if (expr[0] == '>') {
        op = expr[1] == '=' ? 3 : 2;
        expr += op == 3 ? 2 : 1;
    } else {
        return 0;
    }
    if (*expr == '\0') return 0;
    c = ver_cmp(ver, expr);
    switch (op) {
        case 0:  return c < 0;
        case 1:  return c <= 0;
        case 2:  return c > 0;
        default: return c >= 0;
    }
}

/* map_lookup_range -- the first `name@<op><ver>` row in this file whose
 * comparison holds for <ver>. Ranges cannot be ordered by specificity the way
 * exact keys can, so file order is the tie-break, exactly as in _pkgmap_range. */
static int map_lookup_range(Str *out, const char *map_path, const char *name,
                            const char *ver) {
    char *buf;
    size_t len;
    size_t pos = 0;
    Line line;
    int found = 0;
    size_t nlen = strlen(name);

    buf = slurp(map_path, &len);
    if (buf == NULL) return 0;
    while (!found && next_line(buf, len, &pos, &line)) {
        const char *p = line.start;
        size_t remain = line.len;
        Str expr;

        while (remain > 0 && is_space(*p)) { p++; remain--; }
        if (remain <= nlen + 1 || strncmp(p, name, nlen) != 0 || p[nlen] != '@') continue;
        p += nlen + 1;
        remain -= nlen + 1;
        if (remain == 0 || (*p != '<' && *p != '>')) continue;
        /* The comparison facet: the operator, then the version. Stopping at the
         * first '=' the way the row separator is normally found would cut
         * `<=3.22` down to `<`, so the operator is consumed first and only
         * digits and dots after it (_pkgmap_range reads it with a sed for the
         * same reason). */
        str_init(&expr);
        str_addc(&expr, *p);
        p++;
        remain--;
        if (remain > 0 && *p == '=') { str_addc(&expr, *p); p++; remain--; }
        while (remain > 0 && ((*p >= '0' && *p <= '9') || *p == '.')) {
            str_addc(&expr, *p);
            p++;
            remain--;
        }
        while (remain > 0 && is_space(*p)) { p++; remain--; }
        if (remain > 0 && *p == '=' && ver_match(ver, str_text(&expr))) {
            row_rhs(out, p + 1, remain - 1);
            found = 1;
        }
        str_free(&expr);
    }
    free(buf);
    return found;
}

/* pkgmap_one -- the logical name resolved to real package name(s), most
 * specific facet first, in <manager>.map then any.map (§1a, _pkgmap_one):
 *
 *   name@trixie    codename      exact
 *   name@3.21.3    version_id    exact
 *   name@3.21      version_id    dotted prefix, longest first (then name@3)
 *   name@<=3.21    version_id    comparison, first matching row wins
 *   name@x86_64    arch          exact
 *   name           -             the bare row
 *
 * An unlisted name passes through unchanged (§1). */
void osr_pkgmap_resolve(Str *out, const char *name) {
    const char *codename = env_str("OSR_CODENAME", NULL);
    const char *version  = env_str("OSR_VERSION_ID", NULL);
    const char *arch     = env_str("OSR_ARCH", NULL);
    Str key;
    Str map;
    int stage;
    int j;
    int done = 0;

    str_init(&key);
    str_init(&map);

    /* The two map paths, rebuilt per probe: <manager>.map then any.map. */
    #define OSR_MAP_PATH(which) do {                                  \
        str_reset(&map);                                              \
        str_addz(&map, env_str("OSR_LIB", "lib"));                    \
        str_addz(&map, "/pkgmap/");                                   \
        if ((which) == 0) {                                           \
            str_addz(&map, osr_mod_pkg());                            \
            str_addz(&map, ".map");                                   \
        } else {                                                      \
            str_addz(&map, "any.map");                                \
        }                                                             \
    } while (0)

    /* stage 0 codename, 1 version_id, 2 version prefixes, 3 ranges, 4 arch,
     * 5 the bare name. */
    for (stage = 0; !done && stage <= 5; stage++) {
        if (stage == 0 && codename == NULL) continue;
        if ((stage == 1 || stage == 2 || stage == 3) && version == NULL) continue;
        if (stage == 4 && arch == NULL) continue;

        if (stage == 2) {
            /* dotted prefixes, longest first: 3.21.3 -> 3.21 -> 3 */
            size_t plen = strlen(version);
            for (;;) {
                size_t i = plen;
                while (i > 0 && version[i - 1] != '.') i--;
                if (i == 0) break;                   /* no dot left to drop */
                plen = i - 1;
                str_reset(&key);
                str_addz(&key, name);
                str_addc(&key, '@');
                str_add(&key, version, plen);
                for (j = 0; j < 2; j++) {
                    OSR_MAP_PATH(j);
                    if (map_lookup(out, str_text(&map), str_text(&key))) { done = 1; break; }
                }
                if (done) break;
            }
            continue;
        }

        if (stage == 3) {
            for (j = 0; j < 2; j++) {
                OSR_MAP_PATH(j);
                if (map_lookup_range(out, str_text(&map), name, version)) { done = 1; break; }
            }
            continue;
        }

        str_reset(&key);
        str_addz(&key, name);
        if (stage == 0) { str_addc(&key, '@'); str_addz(&key, codename); }
        if (stage == 1) { str_addc(&key, '@'); str_addz(&key, version); }
        if (stage == 4) { str_addc(&key, '@'); str_addz(&key, arch); }
        for (j = 0; j < 2; j++) {
            OSR_MAP_PATH(j);
            if (map_lookup(out, str_text(&map), str_text(&key))) { done = 1; break; }
        }
    }
    #undef OSR_MAP_PATH

    str_free(&key);
    str_free(&map);
    if (!done) str_addz(out, name);          /* not listed -> unchanged */
}

/* native_installed -- the per-manager probe _native_installed used. */
static int native_installed(const char *pkg) {
    const char *mgr = osr_mod_pkg();
    char *argv[6];
    int devnull_rc;

    if (strcmp(mgr, "apt") == 0) {
        argv[0] = (char *)"dpkg"; argv[1] = (char *)"-s"; argv[2] = (char *)pkg; argv[3] = NULL;
    } else if (strcmp(mgr, "dnf") == 0) {
        argv[0] = (char *)"rpm"; argv[1] = (char *)"-q"; argv[2] = (char *)pkg; argv[3] = NULL;
    } else if (strcmp(mgr, "pacman") == 0) {
        argv[0] = (char *)"pacman"; argv[1] = (char *)"-Q"; argv[2] = (char *)pkg; argv[3] = NULL;
    } else if (strcmp(mgr, "apk") == 0) {
        argv[0] = (char *)"apk"; argv[1] = (char *)"info"; argv[2] = (char *)"-e";
        argv[3] = (char *)pkg; argv[4] = NULL;
    } else if (strcmp(mgr, "xbps") == 0) {
        argv[0] = (char *)"xbps-query"; argv[1] = (char *)pkg; argv[2] = NULL;
    } else if (strcmp(mgr, "portage") == 0) {
        if (osr_have_cmd("qlist")) {
            argv[0] = (char *)"qlist"; argv[1] = (char *)"-I"; argv[2] = (char *)"-e";
            argv[3] = (char *)pkg; argv[4] = NULL;
        } else {
            argv[0] = (char *)"portageq"; argv[1] = (char *)"has_version";
            argv[2] = (char *)"/"; argv[3] = (char *)pkg; argv[4] = NULL;
        }
    } else {
        return 0;
    }
    devnull_rc = osr_run_quiet(argv);
    return devnull_rc == 0;
}

int osr_pkg_installed(const char *name) {
    Str rhs;
    int ok = 1;
    const char *p;

    str_init(&rhs);
    osr_pkgmap_resolve(&rhs, name);
    p = str_text(&rhs);
    if (strncmp(p, "aur:", 4) == 0) {
        char *argv[4];
        argv[0] = (char *)"pacman"; argv[1] = (char *)"-Q";
        argv[2] = (char *)(p + 4); argv[3] = NULL;
        ok = osr_run_quiet(argv) == 0;
    } else if (strncmp(p, "script:", 7) == 0 || strncmp(p, "source:", 7) == 0 ||
               strncmp(p, "cargo:", 6) == 0) {
        ok = osr_have_cmd(name);
    } else {
        Str word;
        str_init(&word);
        while (*p != '\0' && ok) {
            while (is_space(*p)) p++;
            str_reset(&word);
            while (*p != '\0' && !is_space(*p)) str_addc(&word, *p++);
            if (word.len > 0) ok = native_installed(str_text(&word));
        }
        str_free(&word);
    }
    str_free(&rhs);
    return ok;
}

/* --- held / pinned packages (G2) -------------------------------------------
 *
 * "Never override user-defined state" applied to packages: a hold, an
 * IgnorePkg, an exclude= or a package.mask entry is a decision the user made,
 * and an installer that walks over it is worse than one that skips the
 * package and says so.
 *
 * Each manager is asked the same question lib/pkg.sh's _native_held asked it,
 * with the same argv where a tool answers (apt-mark) and the same grep where a
 * config file does -- the config walks are left to grep rather than reopened
 * here because `grep -r` over /etc/yum.repos.d and /etc/portage is a directory
 * traversal this file has no other reason to own.
 */

/* is_word_char -- grep's word constituents: [A-Za-z0-9_]. */
static int is_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

/* word_match -- grep -w: `word` appears in text delimited by non-word chars.
 * grep's word characters are [A-Za-z0-9_], so a package name's `-` and `.`
 * are delimiters there too, exactly as they are for grep. */
static int word_match(const char *text, const char *word) {
    size_t wl = strlen(word);
    const char *p = text;
    if (wl == 0) return 0;
    while ((p = strstr(p, word)) != NULL) {
        int lok = (p == text) || !is_word_char(p[-1]);
        int rok = !is_word_char(p[wl]);
        if (lok && rok) return 1;
        p++;
    }
    return 0;
}

/* line_match -- grep -x: one whole line of text equals word. */
static int line_match(const char *text, const char *word) {
    size_t pos = 0;
    Line line;
    size_t wl = strlen(word);
    while (next_line(text, strlen(text), &pos, &line)) {
        if (line.len == wl && memcmp(line.start, word, wl) == 0) return 1;
    }
    return 0;
}

/* uncommented -- is there any line here that is not a `#` comment? The tail of
 * portage's `grep -rhw ... | grep -qv '^[[:space:]]*#'`. */
static int uncommented(const char *text) {
    size_t pos = 0;
    Line line;
    while (next_line(text, strlen(text), &pos, &line)) {
        const char *p = line.start;
        size_t n = line.len;
        while (n > 0 && is_space(*p)) { p++; n--; }
        if (n > 0 && *p != '#') return 1;
    }
    return 0;
}

static int native_held(const char *pkg) {
    const char *mgr = osr_mod_pkg();
    Str out;
    int held = 0;

    str_init(&out);
    if (strcmp(mgr, "apt") == 0) {
        char *argv[3];
        argv[0] = (char *)"apt-mark"; argv[1] = (char *)"showhold"; argv[2] = NULL;
        if (osr_run_capture(argv, &out)) held = line_match(str_text(&out), pkg);
    } else if (strcmp(mgr, "pacman") == 0) {
        char *argv[5];
        argv[0] = (char *)"grep"; argv[1] = (char *)"-E";
        argv[2] = (char *)"^[[:space:]]*IgnorePkg";
        argv[3] = (char *)"/etc/pacman.conf"; argv[4] = NULL;
        if (osr_run_capture(argv, &out)) held = word_match(str_text(&out), pkg);
    } else if (strcmp(mgr, "dnf") == 0) {
        Str re;
        char *argv[7];
        str_init(&re);
        str_addz(&re, "^[[:space:]]*exclude=.*\\b");
        str_addz(&re, pkg);
        str_addz(&re, "\\b");
        argv[0] = (char *)"grep"; argv[1] = (char *)"-rl"; argv[2] = (char *)"-E";
        argv[3] = re.p; argv[4] = (char *)"/etc/dnf/dnf.conf";
        argv[5] = (char *)"/etc/yum.repos.d"; argv[6] = NULL;
        if (osr_run_capture(argv, &out)) held = out.len > 0;
        str_free(&re);
    } else if (strcmp(mgr, "portage") == 0) {
        char *argv[5];
        argv[0] = (char *)"grep"; argv[1] = (char *)"-rhw"; argv[2] = (char *)pkg;
        argv[3] = (char *)"/etc/portage/package.mask"; argv[4] = NULL;
        if (osr_run_capture(argv, &out)) held = uncommented(str_text(&out));
    }
    str_free(&out);
    return held;
}

/* OSR_APT_BOOTSTRAP_LISTS -- the sources.list.d files os-rice writes ITSELF to
 * bootstrap a vendor repo no Debian/Ubuntu archive carries (today just Yandex
 * Browser's, from provide_yandex_browser in lib/build.sh). Overridable so a
 * test can point the whole check at a sandbox. */
#define APT_BOOTSTRAP_LISTS_DEFAULT "/etc/apt/sources.list.d/yandex-browser.list"

/* list_uri -- the first http(s) token on a `deb` line of an apt list file, the
 * awk one-liner in lib/pkg.sh's _apt_prune_bootstrap_lists. */
static int list_uri(Str *out, const char *path) {
    char *buf;
    size_t len, pos = 0;
    Line line;
    int found = 0;

    buf = slurp(path, &len);
    if (buf == NULL) return 0;
    while (!found && next_line(buf, len, &pos, &line)) {
        const char *p = line.start;
        const char *end = line.start + line.len;
        size_t n;
        while (p < end && is_space(*p)) p++;
        if ((size_t)(end - p) < 3 || memcmp(p, "deb", 3) != 0) continue;
        if (p + 3 < end && !is_space(p[3])) continue;    /* deb-src is not deb */
        p += 3;
        while (p < end && !found) {
            while (p < end && is_space(*p)) p++;
            n = 0;
            while (p + n < end && !is_space(p[n])) n++;
            if (n == 0) break;
            if ((n > 7 && memcmp(p, "http://", 7) == 0) ||
                (n > 8 && memcmp(p, "https://", 8) == 0)) {
                str_add(out, p, n);
                found = 1;
            }
            p += n;
        }
    }
    free(buf);
    return found;
}

/* prune_one -- the body of the loop below, for a single bootstrap list. */
static void prune_one(const char *ours) {
    Str uri, dir, parent, hits;
    char *argv[6];
    size_t pos = 0;
    Line line;
    int other = 0;

    str_init(&uri);
    if (!list_uri(&uri, ours)) { str_free(&uri); return; }

    /* The directories to search are derived from the list's own path -- in
     * production /etc/apt/sources.list.d and /etc/apt/sources.list, exactly
     * what lib/pkg.sh names, and in a sandbox whatever the list is in. */
    str_init(&dir);
    str_init(&parent);
    {
        const char *slash = strrchr(ours, '/');
        const char *up;
        if (slash == NULL) { str_free(&uri); str_free(&dir); str_free(&parent); return; }
        str_add(&dir, ours, (size_t)(slash - ours));
        up = strrchr(str_text(&dir), '/');
        str_add(&parent, str_text(&dir), up ? (size_t)(up - str_text(&dir)) : dir.len);
        str_addz(&parent, "/sources.list");
    }

    /* Substring match on purpose: the vendor writes the URI with a trailing
     * slash and a deb822 .sources file puts it on a URIs: line -- both still
     * CONTAIN ours. */
    str_init(&hits);
    argv[0] = (char *)"grep"; argv[1] = (char *)"-rlF"; argv[2] = uri.p;
    argv[3] = parent.p; argv[4] = dir.p; argv[5] = NULL;
    if (osr_run_capture(argv, &hits)) {
        while (!other && next_line(str_text(&hits), hits.len, &pos, &line)) {
            if (line.len == 0) continue;
            if (line.len == strlen(ours) && memcmp(line.start, ours, line.len) == 0) continue;
            other = 1;
            osr_infof("dropping %s - %.*s already describes %s "
                      "(two signed-by values for one repo is fatal to apt 3.0)",
                      ours, (int)line.len, line.start, str_text(&uri));
        }
    }
    if (other) {
        char *rm[4];
        rm[0] = (char *)"rm"; rm[1] = (char *)"-f"; rm[2] = (char *)ours; rm[3] = NULL;
        osr_run_root(rm);
    }
    str_free(&hits);
    str_free(&parent);
    str_free(&dir);
    str_free(&uri);
}

/* apt_prune_bootstrap_lists -- drop one of our bootstrap lists once the vendor
 * package describes the same repo itself, BEFORE any apt call.
 *
 * Not cosmetic: our list pins signed-by=/etc/apt/keyrings/*.asc and the
 * vendor's postinst writes its own list for the same URI with
 * signed-by=/usr/share/keyrings/*.gpg -- and apt 3.0 (Debian 13+) treats one
 * repo described twice with different signed-by values as fatal, which breaks
 * every later apt call on the box, not just ours.
 */
static void apt_prune_bootstrap_lists(void) {
    const char *lists = env_str("OSR_APT_BOOTSTRAP_LISTS", APT_BOOTSTRAP_LISTS_DEFAULT);
    const char *p = lists;

    if (strcmp(osr_mod_pkg(), "apt") != 0) return;
    while (*p != '\0') {                      /* the field split sh does on $IFS */
        Str one;
        size_t n = 0;
        while (*p != '\0' && is_space(*p)) p++;
        while (p[n] != '\0' && !is_space(p[n])) n++;
        if (n == 0) break;
        str_init(&one);
        str_add(&one, p, n);
        if (file_exists(str_text(&one))) prune_one(str_text(&one));
        str_free(&one);
        p += n;
    }
}

/* pkg_refresh -- once per process, lazily, right before the first install:
 * a fresh container has no package lists yet. */
static int refreshed = 0;

static void pkg_refresh(void) {
    const char *mgr = osr_mod_pkg();
    char *argv[8];

    if (refreshed) return;
    refreshed = 1;
    if (strcmp(mgr, "apt") == 0) {
        apt_prune_bootstrap_lists();
        argv[0] = (char *)"env"; argv[1] = (char *)"DEBIAN_FRONTEND=noninteractive";
        argv[2] = (char *)"apt-get"; argv[3] = (char *)"update"; argv[4] = (char *)"-q";
        argv[5] = (char *)"-o"; argv[6] = (char *)"Dpkg::Use-Pty=0";
        argv[7] = NULL;
    } else if (strcmp(mgr, "dnf") == 0) {
        argv[0] = (char *)"dnf"; argv[1] = (char *)"-q"; argv[2] = (char *)"makecache"; argv[3] = NULL;
    } else if (strcmp(mgr, "pacman") == 0) {
        argv[0] = (char *)"pacman"; argv[1] = (char *)"-Sy"; argv[2] = (char *)"--noconfirm"; argv[3] = NULL;
    } else if (strcmp(mgr, "apk") == 0) {
        argv[0] = (char *)"apk"; argv[1] = (char *)"update"; argv[2] = NULL;
    } else if (strcmp(mgr, "xbps") == 0) {
        argv[0] = (char *)"xbps-install"; argv[1] = (char *)"-S"; argv[2] = NULL;
    } else if (strcmp(mgr, "portage") == 0) {
        /* getuto provisions the binary-package signing keyring the official
         * binhost requires (verify-signature=true), and emerge-webrsync takes a
         * tree SNAPSHOT, which is faster than an rsync --sync. Both are
         * best-effort: a box without them still syncs the old way. */
        if (osr_have_cmd("getuto")) {
            char *g[2];
            g[0] = (char *)"getuto"; g[1] = NULL;
            osr_run_root_quiet(g);
        }
        if (osr_have_cmd("emerge-webrsync")) {
            argv[0] = (char *)"emerge-webrsync"; argv[1] = NULL;
        } else {
            argv[0] = (char *)"emerge"; argv[1] = (char *)"--sync"; argv[2] = (char *)"--quiet";
            argv[3] = NULL;
        }
    } else {
        return;
    }
    if (osr_run_root(argv) != 0) osr_warn("package index refresh failed - continuing");
}

/* --- the provider methods (§4) ---------------------------------------------
 *
 * A resolved spec carries its install method as a `<method>:` prefix, and each
 * method owns its own idempotency probe -- which is the point of tagging them:
 * "is this installed" has a different answer for a crate, an AUR package and a
 * curl-piped installer, and none of them is the native package database.
 */
typedef enum {
    M_NATIVE = 0, M_SCRIPT, M_SOURCE, M_CARGO, M_AUR, M_OTHER
} Method;

static Method spec_method(const char *rhs) {
    if (strncmp(rhs, "script:", 7) == 0) return M_SCRIPT;
    if (strncmp(rhs, "source:", 7) == 0) return M_SOURCE;
    if (strncmp(rhs, "cargo:", 6) == 0)  return M_CARGO;
    if (strncmp(rhs, "aur:", 4) == 0)    return M_AUR;
    /* The providers lib/pkg.sh names but does not implement either (G1 is
     * still open): repo:, tarball:, brew:, flatpak:. */
    if (strchr(rhs, ':') != NULL && strncmp(rhs, "http", 4) != 0) return M_OTHER;
    return M_NATIVE;
}

/* spec_arg -- the text after the method tag. */
static const char *spec_arg(const char *rhs) {
    const char *p = strchr(rhs, ':');
    return p != NULL ? p + 1 : rhs;
}

/* --- script: (a piped installer) -------------------------------------------
 * Spec: script:<url> [args...] -- the args are forwarded to the installer.
 * Probe: the logical name is the command it is supposed to leave behind.
 */

static int via_script(const char *name, const char *spec) {
    Str words;
    char **argv;
    size_t argc = 0;
    const char *backend;
    const char *url;
    char *p;
    int fds[2];
    pid_t dl;
    int rc;
    int status;

    if (osr_have_cmd(name)) {
        osr_infof("%s already present (script) - skipping", name);
        return 1;
    }

    /* `script:<url> [args...]`: the sh side word-split this deliberately. */
    str_init(&words);
    str_addz(&words, spec);
    p = words.p;
    while (*p != '\0' && !is_space(*p)) p++;
    argv = (char **)calloc(8 + words.len, sizeof(char *));
    if (argv == NULL) osr_die_oom();
    url = words.p;
    argv[argc++] = (char *)"sh";
    argv[argc++] = (char *)"-s";
    argv[argc++] = (char *)"--";
    while (*p != '\0') {
        while (is_space(*p)) *p++ = '\0';
        if (*p == '\0') break;
        argv[argc++] = p;
        while (*p != '\0' && !is_space(*p)) p++;
    }
    argv[argc] = NULL;

    backend = osr_fetch_ensure();
    if (*backend == '\0') {
        osr_warn("no downloader found (need curl, wget, or busybox)");
        free(argv);
        str_free(&words);
        return 0;
    }

    osr_infof("installing %s via script installer", name);
    if (pipe(fds) != 0) { free(argv); str_free(&words); return 0; }
    dl = osr_fetch_child(backend, url, fds[1]);
    close(fds[1]);
    if (dl < 0) { close(fds[0]); free(argv); str_free(&words); return 0; }
    rc = osr_run_user_in(argv, fds[0]);
    close(fds[0]);
    waitpid(dl, &status, 0);
    free(argv);
    str_free(&words);
    if (rc != 0) {
        osr_warnf("script install failed for %s", name);
        return 0;
    }
    return 1;
}

/* --- cargo: (a crate) ------------------------------------------------------
 * Spec: cargo:<crate>, installed as OSR_USER into ~/.cargo/bin, --locked.
 * Needs a toolchain, so `rust` is listed before any cargo: row (§4). Probe:
 * the binary in ~/.cargo/bin, asked AS THE USER -- root cannot assume it can
 * see that home.
 */

/* user_test_x -- `as_user test -x <path>`. */
static int user_test_x(const char *path) {
    char *argv[4];
    argv[0] = (char *)"test"; argv[1] = (char *)"-x"; argv[2] = (char *)path; argv[3] = NULL;
    return osr_run_user(argv) == 0;
}

static void cargo_path(Str *out, const char *leaf) {
    str_reset(out);
    str_addz(out, osr_mod_home());
    str_addz(out, "/.cargo/bin/");
    str_addz(out, leaf);
}

int osr_pkg_cargo(const char *name, const char *crate) {
    Str bin, cargo, binstall;
    char *argv[6];
    int ok = 0;
    int rc;

    str_init(&bin); str_init(&cargo); str_init(&binstall);
    cargo_path(&bin, name);
    cargo_path(&cargo, "cargo");
    cargo_path(&binstall, "cargo-binstall");

    if (user_test_x(str_text(&bin))) {
        osr_infof("%s already present (cargo) - skipping", name);
        ok = 1;
        goto done;
    }
    /* Fatal, not a warning: lib/pkg.sh spelled this `error`, and a missing
     * toolchain is a manifest-order bug (§4) the run cannot install around. */
    if (!user_test_x(str_text(&cargo)))
        osr_die("cargo not found for %s - install 'rust' before any cargo: package", name);
    /* binstall first (modules/rust.sh installs it): a prebuilt binary where
     * upstream ships one. Not every crate/arch has an asset, so a failure
     * falls through to the source build rather than ending the install. */
    if (user_test_x(str_text(&binstall))) {
        osr_infof("installing %s via cargo-binstall (%s)", name, crate);
        argv[0] = cargo.p; argv[1] = (char *)"binstall";
        argv[2] = (char *)"--no-confirm"; argv[3] = (char *)crate; argv[4] = NULL;
        if (osr_run_user(argv) == 0) { ok = 1; goto done; }
        osr_warnf("cargo-binstall failed for %s - falling back to a source build", name);
    }
    osr_infof("installing %s via cargo (%s)", name, crate);
    argv[0] = cargo.p; argv[1] = (char *)"install";
    argv[2] = (char *)"--locked"; argv[3] = (char *)crate; argv[4] = NULL;
    rc = osr_run_user(argv);
    if (rc != 0) osr_die("cargo install failed for %s (exit %d)", name, rc);
    ok = 1;
done:
    str_free(&bin); str_free(&cargo); str_free(&binstall);
    return ok;
}

/* --- aur: (an AUR package via paru/yay) ------------------------------------
 * Probe is `pacman -Q`, not `command -v`: an AUR package registers in the
 * pacman database like a native one, and its binary is often named differently
 * from the package (visual-studio-code-insiders-bin -> code-insiders).
 */

/* aur_helper -- resolved at install time, not during detection: paru is often
 * BUILT mid-run, so a helper looked up once up front would miss it. */
static const char *aur_helper(void) {
    if (osr_have_cmd("paru")) return "paru";
    if (osr_have_cmd("yay")) return "yay";
    return "";
}

static int via_aur(const char *name, const char *pkg) {
    const char *helper;
    char *argv[7];

    {
        char *q[4];
        q[0] = (char *)"pacman"; q[1] = (char *)"-Q"; q[2] = (char *)pkg; q[3] = NULL;
        if (osr_run_quiet(q) == 0) {
            osr_infof("%s already installed (aur) - skipping", pkg);
            return 1;
        }
    }
    helper = aur_helper();
    if (*helper == '\0') {
        osr_warnf("no AUR helper (paru/yay) for %s - install 'paru' before any aur: package", name);
        return 0;
    }
    osr_infof("installing %s via %s (AUR)", pkg, helper);
    argv[0] = (char *)helper; argv[1] = (char *)"-S"; argv[2] = (char *)"--needed";
    argv[3] = (char *)"--noconfirm"; argv[4] = (char *)pkg; argv[5] = NULL;
    if (osr_run_user(argv) != 0) {
        osr_warnf("AUR install failed for %s", pkg);
        return 0;
    }
    return 1;
}

/* pkg_install_via_sh -- run lib/pkg.sh's pkg_install for one package, with the
 * libs it needs sourced around it. The facts are already exported, so
 * detect.sh only defines functions here; nothing is re-detected. */
static int pkg_install_via_sh(const char *name) {
    Str script;
    char *argv[6];
    int rc;

    str_init(&script);
    str_addz(&script, ". \"$OSR_LIB/ui.sh\"; . \"$OSR_LIB/log.sh\"; ");
    str_addz(&script, "for l in detect user net pkg git config build; do ");
    str_addz(&script, "[ -f \"$OSR_LIB/$l.sh\" ] && . \"$OSR_LIB/$l.sh\"; done; ");
    str_addz(&script, "pkg_install \"$1\"");
    argv[0] = (char *)"sh";
    argv[1] = (char *)"-c";
    argv[2] = script.p;
    argv[3] = (char *)"_";
    argv[4] = (char *)name;
    argv[5] = NULL;
    rc = osr_run(argv);
    str_free(&script);
    if (rc != 0) osr_warnf("provider install failed for %s (exit %d)", name, rc);
    return rc == 0;
}

/* via_source -- _via_source: the builder named by the row, with the same
 * `command -v <name>` idempotency probe in front of it (§2, §4). A builder that
 * has been ported runs in this process; anything still living in lib/build.sh
 * goes back through the shell for that one row, which is the last call from the
 * C tier into sh and goes away with the last builder. */
static int via_source(const char *pkg, const char *fn) {
    if (osr_have_cmd(pkg)) {
        osr_infof("%s already present (source) - skipping", pkg);
        return 1;
    }
    if (!osr_build_has(fn)) return pkg_install_via_sh(pkg);
    osr_infof("building %s from source (%s)", pkg, fn);
    if (!osr_build_run(fn)) osr_die("source build failed for %s", pkg);
    return 1;
}

/* via_native -- pass 1: every native row, batched into ONE install command.
 * Already-installed and held packages drop out here, so a rerun with nothing
 * to do runs no package manager at all (§2). */
/* has_sub -- `grep <needle>`: is needle anywhere in these len bytes. */
static int has_sub(const char *hay, size_t len, const char *needle) {
    size_t n = strlen(needle);
    size_t i;

    if (n > len) return 0;
    for (i = 0; i + n <= len; i++) {
        if (memcmp(hay + i, needle, n) == 0) return 1;
    }
    return 0;
}

/* field -- the n-th whitespace-separated field of a line, awk's $n. */
static void field(Str *out, const char *start, size_t len, int n) {
    const char *p = start, *end = start + len;
    int i;

    for (i = 1; i <= n; i++) {
        size_t w = 0;
        while (p < end && is_space(*p)) p++;
        while (p + w < end && !is_space(p[w])) w++;
        if (w == 0) return;
        if (i == n) { str_add(out, p, w); return; }
        p += w;
    }
}

/* xbps_clear_conflicts -- remove installed packages that would make the xbps
 * transaction abort, so the batch can proceed.
 *
 * The case this exists for: two packages are alternative implementations of one
 * thing, and the one we want declares the other's name as a virtual it
 * provides. `unclutter-xfixes` provides `unclutter>=0`, so on a box that
 * already has the original `unclutter` xbps refuses the WHOLE transaction --
 * the other packages in the same `xbps-install` call never land.
 *
 * xbps itself is the authority on what conflicts, not a table here: a dry run
 * (-n) reports every conflict without touching the system, and its lines read
 *
 *   CONFLICT: unclutter-xfixes-1.6_1 with installed pkg unclutter-8_5 (matched by unclutter>=0)
 *
 * Only the `with installed pkg` form is actionable -- the other form ("... in
 * transaction") is two NEW packages disagreeing, where there is nothing
 * installed to remove and no basis to pick a winner.
 *
 * This is the one place os-rice removes a package the user may have installed,
 * which G2 otherwise forbids, so it is fenced in tightly: only what xbps names
 * as blocking THIS transaction, never a held package, and never one something
 * else depends on.
 */
static void xbps_clear_conflicts(char *const todo[], size_t todo_n) {
    Str out;
    char **argv;
    size_t i, pos = 0;
    Line line;

    argv = (char **)calloc(todo_n + 4, sizeof(char *));
    if (argv == NULL) osr_die_oom();
    argv[0] = (char *)"xbps-install";
    argv[1] = (char *)"-n";
    for (i = 0; i < todo_n; i++) argv[2 + i] = todo[i];
    argv[2 + todo_n] = NULL;

    str_init(&out);
    /* A dry run needs the index, and a repo that will not sync is not this
     * function's problem -- the real install is about to report it properly. */
    osr_run_root_capture(argv, &out);
    free(argv);

    while (next_line(str_text(&out), out.len, &pos, &line)) {
        Str new_pkg, old_pkg, name, revdeps;
        char *q[4];

        if (line.len < 9 || memcmp(line.start, "CONFLICT:", 9) != 0) continue;
        if (!has_sub(line.start, line.len, "with installed pkg")) continue;

        /* CONFLICT: <new> with installed pkg <installed> (matched by <pattern>) */
        str_init(&new_pkg); str_init(&old_pkg);
        field(&new_pkg, line.start, line.len, 2);
        field(&old_pkg, line.start, line.len, 6);
        if (old_pkg.len == 0) { str_free(&new_pkg); str_free(&old_pkg); continue; }

        /* <name>-<version>_<revision> -> <name>; xbps-uhelper ships with xbps. */
        str_init(&name);
        q[0] = (char *)"xbps-uhelper"; q[1] = (char *)"getpkgname";
        q[2] = old_pkg.p; q[3] = NULL;
        if (!osr_run_capture(q, &name)) str_reset(&name);
        str_trim_trailing(&name, '\n');
        if (name.len == 0) {
            str_free(&name); str_free(&new_pkg); str_free(&old_pkg);
            continue;
        }
        if (native_held(str_text(&name))) {
            osr_warnf("%s is held - leaving it, %s cannot install",
                      str_text(&old_pkg), str_text(&new_pkg));
            str_free(&name); str_free(&new_pkg); str_free(&old_pkg);
            continue;
        }
        str_init(&revdeps);
        q[0] = (char *)"xbps-query"; q[1] = (char *)"-X"; q[2] = name.p; q[3] = NULL;
        osr_run_capture(q, &revdeps);
        {
            size_t rp = 0;
            Line rl;
            long n = 0;
            while (next_line(str_text(&revdeps), revdeps.len, &rp, &rl)) {
                if (rl.len > 0) n++;
            }
            if (n > 0) {
                osr_warnf("%s conflicts with %s but %ld package(s) need it - leaving it",
                          str_text(&old_pkg), str_text(&new_pkg), n);
                str_free(&revdeps); str_free(&name);
                str_free(&new_pkg); str_free(&old_pkg);
                continue;
            }
        }
        str_free(&revdeps);
        osr_warnf("%s conflicts with %s (same program, different implementation) - replacing it",
                  str_text(&old_pkg), str_text(&new_pkg));
        {
            char *rm[4];
            rm[0] = (char *)"xbps-remove"; rm[1] = (char *)"-y"; rm[2] = name.p; rm[3] = NULL;
            if (osr_run_root_quiet(rm) != 0) {
                osr_warnf("could not remove %s - %s will fail to install",
                          str_text(&name), str_text(&new_pkg));
            }
        }
        str_free(&name); str_free(&new_pkg); str_free(&old_pkg);
    }
    str_free(&out);
}

static int via_native(const char *const names[]) {
    Str todo;                 /* the real package names still to install */
    Str desc;
    char **argv;
    size_t argc = 0;
    size_t i;
    const char *mgr = osr_mod_pkg();
    int rc;

    str_init(&todo);
    for (i = 0; names[i] != NULL; i++) {
        Str rhs;
        const char *p;
        str_init(&rhs);
        osr_pkgmap_resolve(&rhs, names[i]);
        p = str_text(&rhs);
        if (spec_method(p) != M_NATIVE) { str_free(&rhs); continue; }
        while (*p != '\0') {
            Str word;
            str_init(&word);
            while (is_space(*p)) p++;
            while (*p != '\0' && !is_space(*p)) str_addc(&word, *p++);
            if (word.len > 0) {
                if (native_installed(str_text(&word))) {
                    osr_infof("%s already installed - skipping", str_text(&word));
                } else if (native_held(str_text(&word))) {
                    osr_warnf("%s is held/pinned - skipping", str_text(&word));
                } else {
                    if (todo.len > 0) str_addc(&todo, ' ');
                    str_add(&todo, str_text(&word), word.len);
                }
            }
            str_free(&word);
        }
        str_free(&rhs);
    }
    if (todo.len == 0) { str_free(&todo); return 1; }

    pkg_refresh();

    /* Clear anything that would abort the whole xbps transaction before running
     * it, so one conflicting package cannot take the other N down with it. */
    if (strcmp(mgr, "xbps") == 0) {
        Str copy;                 /* split in place, so the batch keeps its own */
        char **words;
        size_t n = 0;
        char *p;

        str_init(&copy);
        str_add(&copy, str_text(&todo), todo.len);
        words = (char **)calloc(todo.len + 2, sizeof(char *));
        if (words == NULL) osr_die_oom();
        p = copy.p;
        while (*p != '\0') {
            while (*p != '\0' && is_space(*p)) *p++ = '\0';
            if (*p == '\0') break;
            words[n++] = p;
            while (*p != '\0' && !is_space(*p)) p++;
        }
        if (n > 0) xbps_clear_conflicts(words, n);
        free(words);
        str_free(&copy);
    }

    /* one install command for everything left */
    argv = (char **)calloc(16 + todo.len, sizeof(char *));
    if (argv == NULL) osr_die_oom();
    if (strcmp(mgr, "apt") == 0) {
        argv[argc++] = (char *)"env";
        argv[argc++] = (char *)"DEBIAN_FRONTEND=noninteractive";
        argv[argc++] = (char *)"apt-get";
        argv[argc++] = (char *)"install";
        argv[argc++] = (char *)"-y";
        /* -q and no dpkg pty: the step log is a file, and apt/dpkg's
         * in-place progress redraws only make the tail window churn. */
        argv[argc++] = (char *)"-q";
        argv[argc++] = (char *)"-o";
        argv[argc++] = (char *)"Dpkg::Use-Pty=0";
    } else if (strcmp(mgr, "dnf") == 0) {
        argv[argc++] = (char *)"dnf"; argv[argc++] = (char *)"install"; argv[argc++] = (char *)"-y";
    } else if (strcmp(mgr, "pacman") == 0) {
        argv[argc++] = (char *)"pacman"; argv[argc++] = (char *)"-S";
        argv[argc++] = (char *)"--needed"; argv[argc++] = (char *)"--noconfirm";
    } else if (strcmp(mgr, "apk") == 0) {
        argv[argc++] = (char *)"apk"; argv[argc++] = (char *)"add";
    } else if (strcmp(mgr, "xbps") == 0) {
        argv[argc++] = (char *)"xbps-install"; argv[argc++] = (char *)"-y";
    } else if (strcmp(mgr, "portage") == 0) {
        argv[argc++] = (char *)"emerge"; argv[argc++] = (char *)"--quiet";
        argv[argc++] = (char *)"--noreplace"; argv[argc++] = (char *)"--getbinpkg";
    } else {
        osr_warnf("no native installer for OSR_PKG='%s'", mgr);
        free(argv);
        str_free(&todo);
        return 0;
    }
    {
        char *p = todo.p;
        while (*p != '\0') {
            while (*p == ' ') *p++ = '\0';
            if (*p == '\0') break;
            argv[argc++] = p;
            while (*p != '\0' && *p != ' ') p++;
        }
    }
    argv[argc] = NULL;

    str_init(&desc);
    rc = osr_run_root(argv);
    if (rc != 0) osr_warnf("native install failed (exit %d)", rc);
    str_free(&desc);
    free(argv);
    str_free(&todo);
    return rc == 0;
}

/* osr_pkg_install -- pkg_install: expand, group by method, dispatch. Two
 * passes, and in this order for a reason -- the native batch is what carries
 * the downloaders and toolchains (curl, rust) that a provider row in pass 2
 * may need, so it cannot run second. Pass 2 keeps manifest order, which is the
 * only dependency graph os-rice has (§4). */
int osr_pkg_install(const char *const names[]) {
    size_t i;

    if (!via_native(names)) return 0;

    for (i = 0; names[i] != NULL; i++) {
        Str rhs;
        int ok = 1;
        str_init(&rhs);
        osr_pkgmap_resolve(&rhs, names[i]);
        switch (spec_method(str_text(&rhs))) {
        case M_NATIVE:                                  /* pass 1 had it */
            break;
        case M_SCRIPT:
            ok = via_script(names[i], spec_arg(str_text(&rhs)));
            break;
        case M_CARGO:
            ok = osr_pkg_cargo(names[i], spec_arg(str_text(&rhs)));
            break;
        case M_AUR:
            ok = via_aur(names[i], spec_arg(str_text(&rhs)));
            break;
        case M_SOURCE:
            ok = via_source(names[i], spec_arg(str_text(&rhs)));
            break;
        default:
            osr_warnf("provider '%.*s:' not yet implemented (%s) - covers native/script/source/cargo/aur",
                      (int)(strchr(str_text(&rhs), ':') - str_text(&rhs)),
                      str_text(&rhs), names[i]);
            ok = 0;
            break;
        }
        str_free(&rhs);
        if (!ok) return 0;
    }
    return 1;
}

static int pkg_install_thunk(void *ctx) {
    return osr_pkg_install((const char *const *)ctx);
}

int osr_pkg_install_step(const char *desc, const char *const names[]) {
    return osr_step(desc, pkg_install_thunk, (void *)names);
}


/* --- the command surface --------------------------------------------------
 *
 * `osr pkg <verb>`: the same verbs lib/pkg.sh exposes as shell functions, so
 * anything that is not a C module -- a test, a shell module, install.sh -- can
 * reach this implementation without sourcing pkg.sh. Exit 0 is success, which
 * for `installed` means "yes".
 */
static int pkg_usage(void) {
    fputs("usage: osr pkg <subcommand> [args]\n\n", stderr);
    fputs("  install <names...>  resolve, batch the native ones, dispatch providers\n", stderr);
    fputs("  remove <names...>   remove the native ones that are actually there\n", stderr);
    fputs("  installed <name>    exit 0 when it is installed under its method\n", stderr);
    fputs("  map <name>          what lib/pkgmap resolves that name to\n", stderr);
    return 2;
}

/* osr_pkg_remove -- lib/pkg.sh's pkg_remove: remove native packages (providers
 * own their own). Absent packages are filtered out rather than passed down:
 * every native remover errors on an unknown package, which would make a first
 * run fatal for any module that removes a stack it is replacing (§2 -- a no-op
 * must stay a no-op).
 */
int osr_pkg_remove(const char *const names[]) {
    Str rm;
    const char *mgr = osr_mod_pkg();
    char **argv;
    size_t argc = 0;
    size_t i;
    int rc;

    str_init(&rm);
    for (i = 0; names[i] != NULL; i++) {
        Str rhs;
        const char *p;
        str_init(&rhs);
        osr_pkgmap_resolve(&rhs, names[i]);
        p = str_text(&rhs);
        if (spec_method(p) != M_NATIVE) {
            osr_warnf("pkg_remove skips non-native %s (%s)", names[i], p);
            str_free(&rhs);
            continue;
        }
        while (*p != '\0') {
            Str word;
            str_init(&word);
            while (is_space(*p)) p++;
            while (*p != '\0' && !is_space(*p)) str_addc(&word, *p++);
            if (word.len > 0) {
                if (native_installed(str_text(&word))) {
                    if (rm.len > 0) str_addc(&rm, ' ');
                    str_add(&rm, str_text(&word), word.len);
                } else {
                    osr_infof("%s not installed - nothing to remove", str_text(&word));
                }
            }
            str_free(&word);
        }
        str_free(&rhs);
    }
    if (rm.len == 0) { str_free(&rm); return 1; }

    argv = (char **)calloc(8 + rm.len, sizeof(char *));
    if (argv == NULL) osr_die_oom();
    if (strcmp(mgr, "apt") == 0) {
        argv[argc++] = (char *)"apt-get"; argv[argc++] = (char *)"remove";
        argv[argc++] = (char *)"-y";
    } else if (strcmp(mgr, "dnf") == 0) {
        argv[argc++] = (char *)"dnf"; argv[argc++] = (char *)"remove"; argv[argc++] = (char *)"-y";
    } else if (strcmp(mgr, "pacman") == 0) {
        argv[argc++] = (char *)"pacman"; argv[argc++] = (char *)"-R";
        argv[argc++] = (char *)"--noconfirm";
    } else if (strcmp(mgr, "apk") == 0) {
        argv[argc++] = (char *)"apk"; argv[argc++] = (char *)"del";
    } else if (strcmp(mgr, "xbps") == 0) {
        argv[argc++] = (char *)"xbps-remove"; argv[argc++] = (char *)"-y";
    } else if (strcmp(mgr, "portage") == 0) {
        argv[argc++] = (char *)"emerge"; argv[argc++] = (char *)"--deselect";
        argv[argc++] = (char *)"--quiet";
    } else {
        free(argv);
        str_free(&rm);
        return 1;                       /* the sh case had no branch either */
    }
    {
        char *p = rm.p;
        while (*p != '\0') {
            while (*p != '\0' && is_space(*p)) *p++ = '\0';
            if (*p == '\0') break;
            argv[argc++] = p;
            while (*p != '\0' && !is_space(*p)) p++;
        }
    }
    argv[argc] = NULL;
    rc = osr_run_root(argv);
    free(argv);
    str_free(&rm);
    /* portage's deselect only drops the world entry; the packages themselves
     * go with the depclean that follows it. */
    if (rc == 0 && strcmp(mgr, "portage") == 0) {
        char *dc[4];
        dc[0] = (char *)"emerge"; dc[1] = (char *)"--depclean";
        dc[2] = (char *)"--quiet"; dc[3] = NULL;
        rc = osr_run_root(dc);
    }
    return rc == 0;
}

int osr_pkg_main(int argc, char **argv) {
    if (argc < 2) return pkg_usage();

    if (strcmp(argv[1], "map") == 0 && argc == 3) {
        Str out;
        str_init(&out);
        osr_pkgmap_resolve(&out, argv[2]);
        out_flush(&out);
        str_free(&out);
        return 0;
    }
    if (strcmp(argv[1], "installed") == 0 && argc == 3) {
        return osr_pkg_installed(argv[2]) ? 0 : 1;
    }
    if (strcmp(argv[1], "remove") == 0 && argc >= 3) {
        return osr_pkg_remove((const char *const *)(argv + 2)) ? 0 : 1;
    }
    if (strcmp(argv[1], "install") == 0 && argc >= 3) {
        /* argv is already NULL-terminated by the C runtime, so the tail of the
         * vector IS the NULL-terminated name list osr_pkg_install wants. */
        return osr_pkg_install((const char *const *)(argv + 2)) ? 0 : 1;
    }
    return pkg_usage();
}
