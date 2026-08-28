/* lib/config.c -- lib/config.sh's layering, composing and version-adapting
 * half. See lib/config.h.
 *
 * Two shapes repeat here and are worth naming once:
 *
 *   - a file is TRANSFORMED and then installed (foot's section names,
 *     alacritty's [general], starship's palette table). The transform is a
 *     line filter, the install is osr_install_file, which carries the
 *     once-only .bak and the content-equal skip (§2).
 *   - a file is OWNED only in part (.zshrc, .xprofile): compose the whole
 *     file in memory, write it back as the riced user.
 *
 * Every write goes through the user's identity, never this process's: a module
 * run under sudo must not leave a root-owned dotfile behind.
 *
 * C89 + POSIX.
 */
#include <fcntl.h>
#include <glob.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "config.h"
#include "cmds.h"
#include "module.h"

/* --- local plumbing -------------------------------------------------------- */

static const char *tmp_root(void) {
    const char *t = env_str("TMPDIR", "/tmp");
    return *t != '\0' ? t : "/tmp";
}

/* tmp_path -- "${TMPDIR:-/tmp}/<stem>$$<suffix>", the name lib/config.sh built
 * for its own temporaries. Same shape on purpose: a leftover from a killed run
 * is recognizable as ours either way. */
static void tmp_path(Str *out, const char *stem, const char *suffix) {
    str_reset(out);
    str_addz(out, tmp_root());
    str_addc(out, '/');
    str_addz(out, stem);
    str_addl(out, (long)getpid());
    str_addz(out, suffix);
}

/* dir_of -- `dirname`. */
static void dir_of(Str *out, const char *path) {
    const char *slash = strrchr(path, '/');
    str_reset(out);
    if (slash == NULL) { str_addc(out, '.'); return; }
    if (slash == path) { str_addc(out, '/'); return; }
    str_add(out, path, (size_t)(slash - path));
}

/* write_file -- bytes to a path THIS process owns (a temporary). The installed
 * copy is made by osr_install_file / cp as the user; nothing here writes into
 * the user's home directly. */
static int write_file(const char *path, const char *text, size_t len) {
    FILE *f = fopen(path, "wb");
    int ok;
    if (f == NULL) return 0;
    ok = len == 0 || fwrite(text, 1, len, f) == len;
    if (fclose(f) != 0) ok = 0;
    return ok;
}

/* user_cp -- `as_user cp [-rf] <src> <dst>`. */
static int user_cp(const char *src, const char *dst, int recursive) {
    char *argv[5];
    argv[0] = (char *)"cp";
    argv[1] = (char *)(recursive ? "-rf" : "-f");
    argv[2] = (char *)src;
    argv[3] = (char *)dst;
    argv[4] = NULL;
    return osr_run_user(argv) == 0;
}

/* mkdir_parent -- `as_user mkdir -p "$(dirname "$path")"`. */
static void mkdir_parent(const char *path) {
    Str dir;
    str_init(&dir);
    dir_of(&dir, path);
    osr_mkdir_p(str_text(&dir));
    str_free(&dir);
}

/* install_transformed -- write text to a temporary and install it, which is
 * every `sed ... >"$tmp"; backup_copy "$tmp" "$dst"; rm -f "$tmp"` in
 * lib/config.sh. */
static int install_transformed(const char *stem, const char *suffix,
                               const Str *text, const char *dst) {
    Str tmp;
    int ok;

    str_init(&tmp);
    tmp_path(&tmp, stem, suffix);
    if (!write_file(str_text(&tmp), str_text(text), text->len)) {
        str_free(&tmp);
        osr_die("could not write %s", str_text(&tmp));
    }
    ok = osr_install_file(str_text(&tmp), dst);
    remove(str_text(&tmp));
    str_free(&tmp);
    return ok;
}

/* read_or_die -- a source file that must be there, as bytes. */
static char *read_or_die(const char *path, size_t *len, const char *what) {
    char *buf = slurp(path, len);
    if (buf == NULL) osr_die("%s: not found: %s", what, path);
    return buf;
}

/* tool_version -- the first line of `<tool> --version`, "" when the tool is not
 * on PATH. The parsing differs per tool, so this only fetches. */
static void tool_version(Str *out, const char *tool) {
    Str raw;
    char *argv[3];
    size_t pos = 0;
    Line l;

    str_reset(out);
    if (!osr_have_cmd(tool)) return;
    str_init(&raw);
    argv[0] = (char *)tool; argv[1] = (char *)"--version"; argv[2] = NULL;
    (void)osr_run_capture(argv, &raw);
    if (next_line(str_text(&raw), raw.len, &pos, &l)) str_add(out, l.start, l.len);
    str_free(&raw);
}

/* line_is -- does this line equal that text exactly (a sed `^...$` match). */
static int line_is(const Line *l, const char *text) {
    size_t n = strlen(text);
    return l->len == n && memcmp(l->start, text, n) == 0;
}

/* --- seeded layers --------------------------------------------------------- */

int osr_seed_once(const char *src, const char *dst) {
    struct stat st;

    if (lstat(dst, &st) == 0) {                  /* `[ -e ]`: a dangling symlink counts */
        osr_infof("keeping existing %s (seeded once)", dst);
        return 1;
    }
    mkdir_parent(dst);
    return user_cp(src, dst, 0);
}

int osr_seed_empty(const char *dst) {
    struct stat st;
    char *argv[3];

    if (lstat(dst, &st) == 0) return 1;
    mkdir_parent(dst);
    argv[0] = (char *)"touch"; argv[1] = (char *)dst; argv[2] = NULL;
    return osr_run_user(argv) == 0;
}

/* --- owned blocks ---------------------------------------------------------- */

void osr_compose_block(Str *out, const char *path, const char *name, const char *body) {
    Str begin, end, text;
    char *buf;
    size_t len;

    str_init(&begin);
    str_addz(&begin, "# >>> os-rice:");
    str_addz(&begin, name);
    str_addz(&begin, " >>>");
    str_init(&end);
    str_addz(&end, "# <<< os-rice:");
    str_addz(&end, name);
    str_addz(&end, " <<<");

    /* `_eb_body=$(cat)` -- the command substitution ate the trailing newlines,
     * and the writer below puts exactly one back. */
    str_init(&text);
    str_addz(&text, body);
    str_trim_trailing(&text, '\n');

    buf = slurp(path, &len);
    if (buf != NULL) {
        size_t pos = 0;
        Line line;
        int has_marker = 0;
        size_t i;
        /* `grep -qF "$_eb_begin"` -- a SUBSTRING search, deliberately not the
         * exact-line test the rewrite uses. The two disagree on a file whose
         * last line had no newline when the block was first appended, and
         * reproducing that disagreement is the point: what the sh version
         * produced there is what is on disk today. */
        for (i = 0; i + begin.len <= len && !has_marker; i++) {
            if (memcmp(buf + i, str_text(&begin), begin.len) == 0) has_marker = 1;
        }
        if (has_marker) {
            int skip = 0;
            while (next_line(buf, len, &pos, &line)) {
                if (line_is(&line, str_text(&begin))) { skip = 1; continue; }
                if (line_is(&line, str_text(&end)))   { skip = 0; continue; }
                if (!skip) {
                    str_add(out, line.start, line.len);
                    str_addc(out, '\n');
                }
            }
        } else {
            str_add(out, buf, len);
        }
        free(buf);
    }

    str_add(out, str_text(&begin), begin.len);
    str_addc(out, '\n');
    str_add(out, str_text(&text), text.len);
    str_addc(out, '\n');
    str_add(out, str_text(&end), end.len);
    str_addc(out, '\n');

    str_free(&text); str_free(&begin); str_free(&end);
}

int osr_ensure_block(const char *path, const char *name, const char *body) {
    Str out, tmp;
    int ok;

    mkdir_parent(path);
    str_init(&out);
    osr_compose_block(&out, path, name, body);
    str_init(&tmp);
    tmp_path(&tmp, "osr-block-", "");
    if (!write_file(str_text(&tmp), str_text(&out), out.len)) {
        str_free(&out); str_free(&tmp);
        return 0;
    }
    ok = user_cp(str_text(&tmp), path, 0);
    remove(str_text(&tmp));
    str_free(&out); str_free(&tmp);
    return ok;
}

/* loader_body -- the two-line loader both drop-in dirs get: source every file
 * in lexical order, then drop the loop variable. `dir` is expanded here (it is
 * the installer's answer), `$_f` is not (it is the shell's, at login). */
static void loader_body(Str *out, const char *dir, const char *glob_suffix) {
    str_reset(out);
    str_addz(out, "for _f in \"");
    str_addz(out, dir);
    str_addz(out, "\"/*");
    str_addz(out, glob_suffix);
    str_addz(out, "; do [ -r \"$_f\" ] && . \"$_f\"; done\nunset _f\n");
}

int osr_install_zsh_loader(const char *rc_dir, const char *zshrc) {
    Str body;
    int ok;
    str_init(&body);
    loader_body(&body, rc_dir, ".zsh");
    ok = osr_ensure_block(zshrc, "loader", str_text(&body));
    str_free(&body);
    return ok;
}

int osr_install_zsh_zshenv(const char *zshenv) {
    /* Debian and Ubuntu ship an /etc/zsh/zshrc that calls compinit before
     * ~/.zshrc is read, so no rc.d layer can get in front of it -- and
     * oh-my-zsh then calls compinit again with its own fpath, ~82 ms thrown
     * away. skip_global_compinit is the opt-out those distros document. On a
     * distro whose /etc/zsh/zshrc never reads it, this is an unused
     * assignment, so it ships unconditionally. */
    return osr_ensure_block(zshenv, "zshenv", "skip_global_compinit=1\n");
}

int osr_install_xprofile_loader(const char *dir, const char *xprofile) {
    Str body;
    int ok;
    str_init(&body);
    loader_body(&body, dir, ".sh");
    ok = osr_ensure_block(xprofile, "xprofile-loader", str_text(&body));
    str_free(&body);
    return ok;
}

/* --- composed configs ------------------------------------------------------ */

/* PY_MERGE -- lib/config.sh's heredoc, byte for byte. The merge stays in
 * python3 rather than becoming C: json.dump's key order, two-space indent and
 * ensure_ascii=False are the output contract here, and reimplementing them is
 * a way to differ from the installed files, not a way to drop a dependency. */
static const char PY_MERGE[] =
    "import json, sys\n"
    "base = json.load(open(sys.argv[1]))\n"
    "base.update(json.load(open(sys.argv[2])))\n"
    "json.dump(base, sys.stdout, indent=2, ensure_ascii=False)\n"
    "sys.stdout.write(\"\\n\")\n";

/* py_merge -- run it with the script on stdin and stdout on the temp file, the
 * `python3 - "$base" "$frag" >"$tmp" <<'PYEOF'` redirection pair. */
static int py_merge(const char *base, const char *frag, const char *out_path) {
    int fds[2];
    int out_fd;
    pid_t pid;
    int status;

    out_fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (out_fd < 0) return 0;
    if (pipe(fds) != 0) { close(out_fd); return 0; }
    pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); close(out_fd); return 0; }
    if (pid == 0) {
        char *argv[5];
        dup2(fds[0], 0);
        dup2(out_fd, 1);
        close(fds[0]); close(fds[1]); close(out_fd);
        argv[0] = (char *)"python3"; argv[1] = (char *)"-";
        argv[2] = (char *)base; argv[3] = (char *)frag; argv[4] = NULL;
        execvp(argv[0], argv);
        _exit(127);
    }
    close(fds[0]);
    close(out_fd);
    if (write(fds[1], PY_MERGE, sizeof(PY_MERGE) - 1) < 0) { /* reported by the exit status */ }
    close(fds[1]);
    if (waitpid(pid, &status, 0) < 0) return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int osr_compose_json_config(const char *base, const char *frag, const char *dst) {
    Str tmp, name;
    int have_frag = file_exists(frag);
    int have_py = osr_have_cmd("python3");
    int ok;

    if (!file_exists(base)) osr_die("compose_json_config: base not found: %s", base);

    str_init(&name);
    base_of(&name, dst);
    if (!have_frag || !have_py) {
        if (!have_frag) osr_infof("no rice fragment for %s - installing the base", str_text(&name));
        if (!have_py)   osr_warnf("python3 not available - installing %s without the rice theme",
                                  str_text(&name));
        str_free(&name);
        return osr_install_file(base, dst);
    }
    str_free(&name);

    str_init(&tmp);
    tmp_path(&tmp, "osr-json-", ".json");
    if (!py_merge(base, frag, str_text(&tmp))) {
        /* The shell did not check either: python3 wrote nothing, and
         * backup_copy then refused an empty/absent source. Say so instead. */
        remove(str_text(&tmp));
        str_free(&tmp);
        osr_die("compose_json_config: merging %s into %s failed", frag, base);
    }
    ok = osr_install_file(str_text(&tmp), dst);
    remove(str_text(&tmp));
    str_free(&tmp);
    return ok;
}

int osr_compose_starship_config(const char *base, const char *frag, const char *dst) {
    Str out;
    char *bbuf, *fbuf;
    size_t blen, flen, pos = 0;
    Line line;
    int ok;

    bbuf = read_or_die(base, &blen, "compose_starship_config: base");
    fbuf = slurp(frag, &flen);
    if (fbuf == NULL) {
        free(bbuf);
        osr_die("compose_starship_config: palette not found: %s", frag);
    }

    str_init(&out);
    /* `sed '/^\[palettes\.theme\]/,$d'`: the base up to its own default palette
     * table, which must stay LAST in the file. */
    while (next_line(bbuf, blen, &pos, &line)) {
        /* sed's address is a PREFIX match, not a whole-line one. */
        if (line.len >= 16 && memcmp(line.start, "[palettes.theme]", 16) == 0) break;
        str_add(&out, line.start, line.len);
        str_addc(&out, '\n');
    }
    str_add(&out, fbuf, flen);
    free(bbuf);
    free(fbuf);

    ok = install_transformed("osr-starship-", ".toml", &out, dst);
    str_free(&out);
    return ok;
}

/* --- configs adapted to the installed app ---------------------------------- */

/* foot_knows_theme_sections -- true when the installed foot understands
 * [colors-dark] (foot >= 1.26). An absent or unparseable foot answers no,
 * which is the safe direction: [colors] is accepted by every version. */
static int foot_knows_theme_sections(void) {
    Str ver;
    const char *p;
    long maj, min;
    int ok = 0;

    str_init(&ver);
    tool_version(&ver, "foot");
    p = str_text(&ver);
    /* `sed -n 's/^foot version: \([0-9][0-9.]*\).*'` -- the prefix is required. */
    if (strncmp(p, "foot version: ", 14) == 0) {
        p += 14;
        if (*p >= '0' && *p <= '9') {
            maj = atol(p);
            while (*p >= '0' && *p <= '9') p++;
            min = *p == '.' ? atol(p + 1) : 0;
            ok = maj > 1 || (maj == 1 && min >= 26);
        }
    }
    str_free(&ver);
    return ok;
}

int osr_install_foot_palette(const char *src, const char *dst) {
    Str out;
    char *buf;
    size_t len, pos = 0;
    Line line;
    int ok;

    if (foot_knows_theme_sections()) return osr_install_file(src, dst);

    buf = read_or_die(src, &len, "install_foot_palette: source");
    str_init(&out);
    while (next_line(buf, len, &pos, &line)) {
        if (line_is(&line, "[colors-dark]"))       str_addz(&out, "[colors]");
        else if (line_is(&line, "[colors-light]")) str_addz(&out, "[colors2]");
        else                                       str_add(&out, line.start, line.len);
        str_addc(&out, '\n');
    }
    free(buf);
    ok = install_transformed("osr-foot-colors-", ".ini", &out, dst);
    str_free(&out);
    return ok;
}

/* alacritty_ver -- "<major> <minor>" of the installed Alacritty, or no version
 * at all when there is none on PATH or the line does not parse. */
static int alacritty_ver(long *maj, long *min) {
    Str ver;
    const char *p;
    int ok = 0;

    str_init(&ver);
    tool_version(&ver, "alacritty");
    p = str_text(&ver);
    if (strncmp(p, "alacritty ", 10) == 0) {
        p += 10;
        if (*p >= '0' && *p <= '9') {
            *maj = atol(p);
            while (*p >= '0' && *p <= '9') p++;
            if (*p == '.' && p[1] >= '0' && p[1] <= '9') {
                *min = atol(p + 1);
                ok = 1;
            }
        }
    }
    str_free(&ver);
    return ok;
}

int osr_install_alacritty_config(const char *src, const char *dst) {
    Str out;
    char *buf;
    size_t len, pos = 0;
    Line line;
    long maj = 0, min = 0;
    int ok;

    /* No parseable version (not installed yet, or a future scheme): assume
     * current. A stray [general] only costs the palette on 0.13; guessing
     * "old" would cost it on every modern build instead. */
    if (!alacritty_ver(&maj, &min)) return osr_install_file(src, dst);

    if (maj == 0 && min < 13) {
        /* Below 0.13 the format was YAML and this file is ignored entirely --
         * say so rather than pretend it landed. */
        osr_warnf("alacritty %ld.%ld predates the TOML config (0.13) - it reads alacritty.yml and will ignore %s",
                  maj, min, dst);
    }
    if (maj > 0 || min >= 14) return osr_install_file(src, dst);

    buf = read_or_die(src, &len, "install_alacritty_config: source");
    str_init(&out);
    while (next_line(buf, len, &pos, &line)) {
        if (line_is(&line, "[general]")) continue;   /* `sed '/^\[general\]$/d'` */
        str_add(&out, line.start, line.len);
        str_addc(&out, '\n');
    }
    free(buf);
    ok = install_transformed("osr-alacritty-", ".toml", &out, dst);
    str_free(&out);
    return ok;
}

/* --- whole directories ------------------------------------------------------ */

int osr_apply_config(const char *name) {
    Str src, dst;
    int ok;

    str_init(&src); str_init(&dst);
    str_addz(&src, osr_mod_theme_dir());
    str_addz(&src, "/config/");
    str_addz(&src, name);
    if (!dir_exists(str_text(&src))) {
        osr_warnf("config '%s' not found in theme (%s) - skipping", name, str_text(&src));
        str_free(&src); str_free(&dst);
        return 1;
    }
    str_addz(&dst, osr_mod_home());
    str_addz(&dst, "/.config/");
    str_addz(&dst, name);
    osr_infof("applying config: %s -> %s", name, str_text(&dst));
    osr_mkdir_p(str_text(&dst));
    /* Trailing /. and /: the CONTENTS are copied, so the directory does not end
     * up nested inside itself on a rerun. */
    str_addz(&src, "/.");
    str_addc(&dst, '/');
    ok = user_cp(str_text(&src), str_text(&dst), 1);
    str_free(&src); str_free(&dst);
    return ok;
}

/* --- Mozilla profiles ------------------------------------------------------- */

/* add_profile -- one directory onto the list, ignoring anything that is not a
 * directory (a profiles.ini row for a profile that was deleted). */
static void add_profile(Str *out, const char *path) {
    if (!dir_exists(path)) return;
    str_addz(out, path);
    str_addc(out, '\n');
}

/* glob_profiles -- the `*.default*` / `*.dev-edition*` fallback, for a profile
 * created before profiles.ini was written. */
static void glob_profiles(Str *out, const char *root, const char *pattern) {
    Str pat;
    glob_t g;
    size_t i;

    str_init(&pat);
    str_addz(&pat, root);
    str_addc(&pat, '/');
    str_addz(&pat, pattern);
    if (glob(str_text(&pat), 0, NULL, &g) == 0) {
        for (i = 0; i < g.gl_pathc; i++) add_profile(out, g.gl_pathv[i]);
        globfree(&g);
    }
    str_free(&pat);
}

void osr_mozilla_profiles(Str *out, const char *root) {
    Str ini;
    char *buf;
    size_t len, pos = 0;
    Line line;

    if (!dir_exists(root)) return;

    str_init(&ini);
    str_addz(&ini, root);
    str_addz(&ini, "/profiles.ini");
    buf = slurp(str_text(&ini), &len);
    str_free(&ini);
    if (buf == NULL) {
        glob_profiles(out, root, "*.default*");
        glob_profiles(out, root, "*.dev-edition*");
        return;
    }

    /* `sed -n 's/^[[:space:]]*Path=//p'`. Path= is relative to the root unless
     * the value is absolute. */
    while (next_line(buf, len, &pos, &line)) {
        const char *p = line.start;
        size_t n = line.len;
        Str path;

        while (n > 0 && is_space(*p)) { p++; n--; }
        if (n < 5 || memcmp(p, "Path=", 5) != 0) continue;
        p += 5; n -= 5;
        if (n == 0) continue;

        str_init(&path);
        if (*p != '/') {
            str_addz(&path, root);
            str_addc(&path, '/');
        }
        str_add(&path, p, n);
        add_profile(out, str_text(&path));
        str_free(&path);
    }
    free(buf);
}

int osr_install_mozilla_layer(const char *root, const char *user_js, const char *user_chrome) {
    Str profiles;
    size_t pos = 0;
    Line line;
    long n = 0;

    str_init(&profiles);
    osr_mozilla_profiles(&profiles, root);
    /* Split on newlines, where lib/config.sh split an unquoted `$(...)` on
     * whitespace: same list for every path without spaces in it, and the right
     * answer for the ones with. */
    while (next_line(str_text(&profiles), profiles.len, &pos, &line)) {
        Str dir, dst;

        if (line.len == 0) continue;
        n++;
        str_init(&dir);
        str_add(&dir, line.start, line.len);
        if (user_js != NULL && *user_js != '\0' && file_exists(user_js)) {
            str_init(&dst);
            str_addz(&dst, str_text(&dir));
            str_addz(&dst, "/user.js");
            osr_install_layer(user_js, str_text(&dst));
            str_free(&dst);
        }
        if (user_chrome != NULL && *user_chrome != '\0' && file_exists(user_chrome)) {
            str_init(&dst);
            str_addz(&dst, str_text(&dir));
            str_addz(&dst, "/chrome");
            osr_mkdir_p(str_text(&dst));
            str_addz(&dst, "/userChrome.css");
            osr_install_layer(user_chrome, str_text(&dst));
            str_free(&dst);
        }
        str_free(&dir);
    }
    str_free(&profiles);

    if (n == 0) {
        osr_warnf("no profile under %s yet - launch the app once, then rerun this module", root);
    } else {
        osr_infof("applied Mozilla layer to %ld profile(s) under %s", n, root);
    }
    return 1;
}

/* same_content -- `cmp -s a b`. */
static int same_content(const char *a, const char *b) {
    char *ba, *bb;
    size_t la, lb;
    int same;

    ba = slurp(a, &la);
    if (ba == NULL) return 0;
    bb = slurp(b, &lb);
    if (bb == NULL) { free(ba); return 0; }
    same = la == lb && (la == 0 || memcmp(ba, bb, la) == 0);
    free(ba);
    free(bb);
    return same;
}

/* write_as_user -- bytes into a file the RICED USER owns: a temporary this
 * process writes, then `as_user cp -f`. A module run under sudo must not leave
 * a root-owned file in the user's home. */
static void write_as_user(const char *path, const char *text, size_t len) {
    Str tmp;
    str_init(&tmp);
    tmp_path(&tmp, "osr-write-", "");
    if (write_file(str_text(&tmp), text, len)) {
        (void)user_cp(str_text(&tmp), path, 0);
        remove(str_text(&tmp));
    }
    str_free(&tmp);
}

/* --- wallpaper -------------------------------------------------------------
 *
 * The shell resolved the wallpaper four times over (hyprpaper, hyprland's env,
 * gtklock, the recorded state) and the copies disagreed. Here it is resolved
 * once, installed once, and every consumer is handed the same absolute path.
 */

/* ends_with -- a case-sensitive suffix test, which is what the sh `case`
 * pattern list was: the uppercase spellings it accepts are listed one by one,
 * and .Gif is deliberately not among them. */
static int ends_with(const char *s, const char *suffix) {
    size_t ls = strlen(s), lf = strlen(suffix);
    return ls >= lf && strcmp(s + ls - lf, suffix) == 0;
}

int osr_is_image(const char *path) {
    static const char *exts[] = {
        ".jpg", ".jpeg", ".png", ".webp", ".bmp", ".gif",
        ".JPG", ".JPEG", ".PNG", ".WEBP", NULL
    };
    size_t i;
    if (!file_exists(path)) return 0;
    for (i = 0; exts[i] != NULL; i++)
        if (ends_with(path, exts[i])) return 1;
    return 0;
}

void osr_theme_wallpapers(Str *out, const char *theme_dir) {
    Str pat;
    glob_t g;
    size_t i;

    if (theme_dir == NULL || *theme_dir == '\0') return;
    str_init(&pat);
    str_addz(&pat, theme_dir);
    str_addz(&pat, "/wallpapers/*");
    if (glob(str_text(&pat), 0, NULL, &g) == 0) {
        for (i = 0; i < g.gl_pathc; i++) {
            if (!osr_is_image(g.gl_pathv[i])) continue;
            str_addz(out, g.gl_pathv[i]);
            str_addc(out, '\n');
        }
        globfree(&g);
    }
    str_free(&pat);
}

/* first_line -- the `| head -n 1 | tr -d '\n'` at the end of
 * osr_theme_wallpaper: one path, no newline. */
static void first_line(Str *out, const Str *lines) {
    size_t pos = 0;
    Line l;
    str_reset(out);
    if (next_line(str_text(lines), lines->len, &pos, &l)) str_add(out, l.start, l.len);
}

void osr_theme_wallpaper(Str *out) {
    const char *dir = osr_mod_theme_dir();
    const char *theme = osr_mod_theme();
    Str all;

    str_reset(out);
    if (*dir == '\0') return;

    if (*theme != '\0') {
        Str key, pick;
        str_init(&key);
        str_addz(&key, "wallpaper.");
        str_addz(&key, theme);
        str_init(&pick);
        osr_state_get(&pick, str_text(&key));
        str_free(&key);
        /* A recorded choice whose file is gone (deleted, checkout moved) falls
         * back to the theme default rather than failing the apply. */
        if (pick.len > 0 && osr_is_image(str_text(&pick))) {
            str_add(out, str_text(&pick), pick.len);
            str_free(&pick);
            return;
        }
        str_free(&pick);
    }

    str_init(&all);
    osr_theme_wallpapers(&all, dir);
    first_line(out, &all);
    str_free(&all);
}

void osr_install_wallpaper_file(Str *out, const char *src) {
    Str dir, dst, base;

    str_reset(out);
    if (src == NULL || *src == '\0') return;

    str_init(&dir);
    str_addz(&dir, osr_mod_home());
    str_addz(&dir, "/Pictures/Wallpapers");
    str_init(&base);
    base_of(&base, src);
    str_init(&dst);
    str_add(&dst, str_text(&dir), dir.len);
    str_addc(&dst, '/');
    str_add(&dst, str_text(&base), base.len);
    str_free(&base);

    /* Rerun-safe (§2): an identical copy is left where it is, timestamps and
     * all, so a reapply does not churn the file every consumer points at. */
    if (!file_exists(str_text(&dst)) || !same_content(src, str_text(&dst))) {
        osr_mkdir_p(str_text(&dir));
        (void)user_cp(src, str_text(&dst), 0);
    }
    str_add(out, str_text(&dst), dst.len);
    str_free(&dir);
    str_free(&dst);
}

void osr_install_wallpaper(Str *out) {
    Str src;
    str_init(&src);
    osr_theme_wallpaper(&src);
    if (src.len == 0) {
        str_reset(out);
        str_free(&src);
        return;
    }
    osr_install_wallpaper_file(out, str_text(&src));
    str_free(&src);
}

int osr_install_wallpaper_layer(const char *src, const char *dst) {
    Str wp, body, out;
    char *buf;
    size_t len, i;
    int ok;
    static const char MARK[] = "{{WALLPAPER_PATH}}";
    const size_t mlen = sizeof(MARK) - 1;

    str_init(&wp);
    osr_install_wallpaper(&wp);

    buf = read_or_die(src, &len, "install_wallpaper_layer: source");
    str_init(&out);
    str_init(&body);
    for (i = 0; i < len; ) {
        if (i + mlen <= len && memcmp(buf + i, MARK, mlen) == 0) {
            str_add(&out, str_text(&wp), wp.len);
            i += mlen;
        } else {
            str_addc(&out, buf[i]);
            i++;
        }
    }
    free(buf);
    str_free(&body);
    str_free(&wp);

    ok = install_transformed("osr-wallpaper-layer-", "", &out, dst);
    str_free(&out);
    return ok;
}

void osr_wallpaper_set_live(const char *img) {
    char *argv[5];

    if (osr_have_cmd("swww")) {
        argv[0] = (char *)"swww"; argv[1] = (char *)"img";
        argv[2] = (char *)img; argv[3] = NULL;
        if (osr_run_user_quiet(argv) != 0) osr_warnf("swww failed to set wallpaper");
    } else if (osr_have_cmd("hyprctl")) {
        Str spec;
        str_init(&spec);
        str_addc(&spec, ',');            /* `,<path>`: every monitor */
        str_addz(&spec, img);
        argv[0] = (char *)"hyprctl"; argv[1] = (char *)"hyprpaper";
        argv[2] = (char *)"wallpaper"; argv[3] = spec.p; argv[4] = NULL;
        if (osr_run_user_quiet(argv) != 0) osr_warnf("hyprpaper failed");
        str_free(&spec);
    } else if (osr_have_cmd("feh")) {
        argv[0] = (char *)"feh"; argv[1] = (char *)"--bg-scale";
        argv[2] = (char *)img; argv[3] = NULL;
        if (osr_run_user_quiet(argv) != 0) osr_warnf("feh failed to set wallpaper");
    } else {
        /* A container, an ssh session, a CI host: nothing to paint, and that
         * is not a failure (§9). The record above is still the answer. */
        osr_infof("no wallpaper setter (headless) - recorded %s", img);
    }
}

void osr_wallpaper_record(const char *img) {
    Str dir, file, line;

    str_init(&dir);
    str_addz(&dir, osr_mod_home());
    str_addz(&dir, "/.config/osr");
    osr_mkdir_p(str_text(&dir));

    /* A bare path on a line, because non-shell consumers read this file (a
     * lock screen, a bar, Proteus). The state file carries the same value
     * keyed by theme, which is what survives a theme switch. */
    str_init(&file);
    str_add(&file, str_text(&dir), dir.len);
    str_addz(&file, "/wallpaper");
    str_init(&line);
    str_addz(&line, img);
    str_addc(&line, '\n');
    write_as_user(str_text(&file), str_text(&line), line.len);
    (void)osr_state_set("wallpaper", img);

    str_free(&line);
    str_free(&file);
    str_free(&dir);
}

int osr_apply_wallpaper(void) {
    Str wp;
    str_init(&wp);
    osr_install_wallpaper(&wp);
    if (wp.len == 0) { str_free(&wp); return 1; }
    osr_wallpaper_record(str_text(&wp));
    osr_wallpaper_set_live(str_text(&wp));
    str_free(&wp);
    return 1;
}

/* seen_basename -- the `case "$seen" in *"|$b"*)` dedup, kept as the same
 * pipe-delimited string so a basename containing a pipe behaves identically. */
static int seen_basename(const Str *seen, const char *base) {
    Str needle;
    int found = 0;
    size_t i;

    str_init(&needle);
    str_addc(&needle, '|');
    str_addz(&needle, base);
    for (i = 0; i + needle.len <= seen->len && !found; i++)
        if (memcmp(str_text(seen) + i, str_text(&needle), needle.len) == 0) found = 1;
    str_free(&needle);
    return found;
}

void osr_wallpaper_library(Str *out) {
    Str seen, theme, pat, base;
    size_t pos = 0;
    Line l;
    glob_t g;
    size_t i;

    str_init(&seen);
    str_init(&base);

    str_init(&theme);
    osr_theme_wallpapers(&theme, osr_mod_theme_dir());
    while (next_line(str_text(&theme), theme.len, &pos, &l)) {
        Str path;
        str_init(&path);
        str_add(&path, l.start, l.len);
        str_reset(&base);
        base_of(&base, str_text(&path));
        str_addc(&seen, '|');
        str_add(&seen, str_text(&base), base.len);
        str_add(out, str_text(&path), path.len);
        str_addc(out, '\n');
        str_free(&path);
    }
    str_free(&theme);

    /* ~/Pictures/Wallpapers is where every image ever applied was copied, so
     * this half accretes into a library across themes. */
    str_init(&pat);
    str_addz(&pat, osr_mod_home());
    str_addz(&pat, "/Pictures/Wallpapers/*");
    if (glob(str_text(&pat), 0, NULL, &g) == 0) {
        for (i = 0; i < g.gl_pathc; i++) {
            if (!osr_is_image(g.gl_pathv[i])) continue;
            str_reset(&base);
            base_of(&base, g.gl_pathv[i]);
            if (seen_basename(&seen, str_text(&base))) continue;
            str_addc(&seen, '|');
            str_add(&seen, str_text(&base), base.len);
            str_addz(out, g.gl_pathv[i]);
            str_addc(out, '\n');
        }
        globfree(&g);
    }
    str_free(&pat);
    str_free(&base);
    str_free(&seen);
}

/* absolute -- the `cd -- "$(dirname)" && pwd`/basename dance the shell did to
 * turn a relative pick into the absolute path every consumer stores. */
static void absolute(Str *out, const char *path) {
    Str dir, base;
    char buf[4096];

    str_reset(out);
    if (*path == '/') { str_addz(out, path); return; }

    str_init(&dir);
    dir_of(&dir, path);
    str_init(&base);
    base_of(&base, path);
    if (realpath(str_text(&dir), buf) != NULL) str_addz(out, buf);
    else                                       str_addz(out, str_text(&dir));
    str_addc(out, '/');
    str_add(out, str_text(&base), base.len);
    str_free(&dir);
    str_free(&base);
}

void osr_choose_wallpaper(Str *out, const char *path) {
    Str src, installed;
    int saved_stdout;

    if (!osr_is_image(path)) osr_die("not an image: %s", path);

    str_init(&src);
    absolute(&src, path);

    /* Keyed by theme on purpose: a wallpaper is part of how a theme looks, so
     * nord -> gruvbox -> nord must bring back the image picked for nord. */
    if (*osr_mod_theme() != '\0') {
        Str key;
        str_init(&key);
        str_addz(&key, "wallpaper.");
        str_addz(&key, osr_mod_theme());
        (void)osr_state_set(str_text(&key), str_text(&src));
        str_free(&key);
    }

    str_init(&installed);
    osr_install_wallpaper_file(&installed, str_text(&src));
    str_free(&src);

    /* This function's stdout IS its answer, so the record's and the setter's
     * logging is moved to stderr for the duration: a "no wallpaper setter"
     * line landing in a caller's `$( )` would corrupt the path it asked for. */
    saved_stdout = dup(1);
    fflush(stdout);
    if (dup2(2, 1) >= 0) {
        osr_wallpaper_record(str_text(&installed));
        osr_wallpaper_set_live(str_text(&installed));
        fflush(stdout);
    }
    if (saved_stdout >= 0) {
        dup2(saved_stdout, 1);
        close(saved_stdout);
    }

    str_reset(out);
    str_add(out, str_text(&installed), installed.len);
    str_free(&installed);
}

/* --- the command ----------------------------------------------------------- */

static int config_usage(void) {
    fputs("usage: osr config <subcommand> [args]\n\n", stderr);
    fputs("  seed-once <src> <dst>            copy only when dst is absent\n", stderr);
    fputs("  seed-empty <dst>                 create it empty when absent\n", stderr);
    fputs("  zsh-loader <rc-dir> <zshrc>      own the rc.d loader block\n", stderr);
    fputs("  zsh-zshenv <zshenv>              own the .zshenv block\n", stderr);
    fputs("  xprofile-loader <dir> <file>     own the .xprofile loader block\n", stderr);
    fputs("  json <base> <fragment> <dst>     base with the rice's keys merged over it\n", stderr);
    fputs("  starship <base> <palette> <dst>  base body plus the rice palette\n", stderr);
    fputs("  foot-palette <src> <dst>         palette, section names per foot version\n", stderr);
    fputs("  alacritty <src> <dst>            config, [general] per alacritty version\n", stderr);
    fputs("  apply <name>                     a theme's config/<name> dir into ~/.config\n", stderr);
    fputs("  mozilla-profiles <root>          every profile dir, one per line\n", stderr);
    fputs("  mozilla <root> <user.js> <userChrome.css>   the layer into each profile\n", stderr);
    fputs("  is-image <path>                  exit 0 when it is one\n", stderr);
    fputs("  wallpapers [theme-dir]           the theme's images, one per line\n", stderr);
    fputs("  wallpaper                        the wallpaper this theme should use\n", stderr);
    fputs("  install-wallpaper                install it, print where it landed\n", stderr);
    fputs("  install-wallpaper-file <src>     install one image into the library\n", stderr);
    fputs("  wallpaper-layer <src> <dst>      a layer with {{WALLPAPER_PATH}} filled in\n", stderr);
    fputs("  wallpaper-record <path>          write it down (file + state)\n", stderr);
    fputs("  wallpaper-set <path>             hand it to the session's setter\n", stderr);
    fputs("  apply-wallpaper                  install + record + paint\n", stderr);
    fputs("  wallpaper-library                every image the user can pick\n", stderr);
    fputs("  choose-wallpaper <path>          make it this theme's wallpaper\n", stderr);
    return 2;
}

/* print_path -- run one of the "yields a path" entry points and print what it
 * yielded, newline and all if it produced one. */
static int print_path(void (*fn)(Str *)) {
    Str out;
    str_init(&out);
    fn(&out);
    out_flush(&out);
    str_free(&out);
    return 0;
}

int osr_config_main(int argc, char **argv) {
    if (argc < 2) return config_usage();

    if (strcmp(argv[1], "seed-once") == 0 && argc == 4)
        return osr_seed_once(argv[2], argv[3]) ? 0 : 1;
    if (strcmp(argv[1], "seed-empty") == 0 && argc == 3)
        return osr_seed_empty(argv[2]) ? 0 : 1;
    if (strcmp(argv[1], "zsh-loader") == 0 && argc == 4)
        return osr_install_zsh_loader(argv[2], argv[3]) ? 0 : 1;
    if (strcmp(argv[1], "zsh-zshenv") == 0 && argc == 3)
        return osr_install_zsh_zshenv(argv[2]) ? 0 : 1;
    if (strcmp(argv[1], "xprofile-loader") == 0 && argc == 4)
        return osr_install_xprofile_loader(argv[2], argv[3]) ? 0 : 1;
    if (strcmp(argv[1], "json") == 0 && argc == 5)
        return osr_compose_json_config(argv[2], argv[3], argv[4]) ? 0 : 1;
    if (strcmp(argv[1], "starship") == 0 && argc == 5)
        return osr_compose_starship_config(argv[2], argv[3], argv[4]) ? 0 : 1;
    if (strcmp(argv[1], "foot-palette") == 0 && argc == 4)
        return osr_install_foot_palette(argv[2], argv[3]) ? 0 : 1;
    if (strcmp(argv[1], "alacritty") == 0 && argc == 4)
        return osr_install_alacritty_config(argv[2], argv[3]) ? 0 : 1;
    if (strcmp(argv[1], "apply") == 0 && argc == 3)
        return osr_apply_config(argv[2]) ? 0 : 1;
    if (strcmp(argv[1], "mozilla-profiles") == 0 && argc == 3) {
        Str out;
        str_init(&out);
        osr_mozilla_profiles(&out, argv[2]);
        out_flush(&out);
        str_free(&out);
        return 0;
    }
    if (strcmp(argv[1], "mozilla") == 0 && argc == 5)
        return osr_install_mozilla_layer(argv[2], argv[3], argv[4]) ? 0 : 1;

    /* The wallpaper family. The ones that yield a path print it WITHOUT a
     * trailing newline, because every shell caller read them through `$( )`
     * and then used the result as a path. */
    if (strcmp(argv[1], "is-image") == 0 && argc == 3)
        return osr_is_image(argv[2]) ? 0 : 1;
    if (strcmp(argv[1], "wallpapers") == 0 && (argc == 2 || argc == 3)) {
        Str out;
        str_init(&out);
        osr_theme_wallpapers(&out, argc == 3 ? argv[2] : osr_mod_theme_dir());
        out_flush(&out);
        str_free(&out);
        return 0;
    }
    if (strcmp(argv[1], "wallpaper") == 0 && argc == 2) return print_path(osr_theme_wallpaper);
    if (strcmp(argv[1], "install-wallpaper") == 0 && argc == 2)
        return print_path(osr_install_wallpaper);
    if (strcmp(argv[1], "install-wallpaper-file") == 0 && argc == 3) {
        Str out;
        str_init(&out);
        osr_install_wallpaper_file(&out, argv[2]);
        out_flush(&out);
        str_free(&out);
        return 0;
    }
    if (strcmp(argv[1], "wallpaper-layer") == 0 && argc == 4)
        return osr_install_wallpaper_layer(argv[2], argv[3]) ? 0 : 1;
    if (strcmp(argv[1], "wallpaper-record") == 0 && argc == 3) {
        osr_wallpaper_record(argv[2]);
        return 0;
    }
    if (strcmp(argv[1], "wallpaper-set") == 0 && argc == 3) {
        osr_wallpaper_set_live(argv[2]);
        return 0;
    }
    if (strcmp(argv[1], "apply-wallpaper") == 0 && argc == 2)
        return osr_apply_wallpaper() ? 0 : 1;
    if (strcmp(argv[1], "wallpaper-library") == 0 && argc == 2) return print_path(osr_wallpaper_library);
    if (strcmp(argv[1], "choose-wallpaper") == 0 && argc == 3) {
        Str out;
        str_init(&out);
        osr_choose_wallpaper(&out, argv[2]);
        out_flush(&out);
        str_free(&out);
        return 0;
    }
    return config_usage();
}
