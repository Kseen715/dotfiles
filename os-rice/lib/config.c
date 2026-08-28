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
    return 2;
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
    return config_usage();
}
