/* lib/theme.c -- the C behind lib/theme.sh: themes as first-class objects.
 *
 * §6a: a rice is a set of PACKAGES, a theme is a set of APPEARANCE LAYERS.
 *
 *   themes/<name>/theme.list    metadata + palette (`key: value` lines)
 *   themes/<name>/config/       the 90-* layers, one dir per app
 *   themes/<name>/wallpapers/   0..n images
 *
 *   list                    every theme name
 *   exists <name>           exit 0 when it is a real theme
 *   lines <file>            the manifest parser, one directive per line
 *   meta <name> <key>       a single-valued field (display, polarity, ...)
 *   configs <name>          the `config:` dirs, one per line
 *   color <name> <role>     one palette entry
 *   hex-dec <#rrggbb>       "r,g,b"
 *   sed <name>              the {{key}} substitution script
 *   session <name>          any | x11 | wayland
 *   rice-themes <rice>      the theme set a rice ships
 *   rice-default <rice>     the rice's own `theme:`
 *   swatch <name>           a run of truecolor blocks for the palette
 *   menu                    the numbered picker (prompt + input on /dev/tty)
 *
 * The comment rule is narrower than a rice manifest's `${line%%#*}` because a
 * palette value IS a hash: `color: bg #2e3440` would strip to nothing. A
 * comment is therefore `#` at the start of a line, or a hash with whitespace
 * on BOTH sides -- and `#rrggbb` never has a space after the hash, so the two
 * can never be confused.
 *
 * What stays in lib/theme.sh: osr_apply_theme_configs (it calls apply_config,
 * a shell function) and osr_resolve_theme's orchestration (it must set shell
 * variables, and it ends the run through error()).
 *
 * What the parser must accept, and what it must refuse, is stated in
 * test/unit_c/theme_test.c.
 *
 * C89 + POSIX.
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "common.h"
#include "cmds.h"
#include "config.h"
#include "module.h"

#include "render.h"

static const char *osr_root(void) { return env_str("OSR_ROOT", "."); }

/* path_of -- "<root>/<a>/<b>[/<c>]" into out. */
static void path_of(Str *out, const char *a, const char *b, const char *c) {
    str_reset(out);
    str_addz(out, osr_root());
    str_addc(out, '/');
    str_addz(out, a);
    str_addc(out, '/');
    str_addz(out, b);
    if (c != NULL) {
        str_addc(out, '/');
        str_addz(out, c);
    }
}

/* --- the manifest parser --------------------------------------------------
 *
 * _osr_theme_lines' five sed substitutions, in order, then `/^$/d`:
 *
 *   s/^[[:space:]]*#.*$//        a whole-line comment vanishes
 *   s/[[:space:]]#[[:space:]].*$//   a trailing ` # comment` is cut off
 *   s/[[:space:]]#$//            ...including one with nothing after it
 *   ltrim, then rtrim   the two [[:space:]] edge trims
 */
static void strip_directive(Str *out, const char *line, size_t len) {
    size_t i;
    size_t start = 0;
    size_t end = len;

    str_reset(out);
    /* rule 1: leading whitespace then '#' -> the line is a comment */
    for (i = 0; i < len && is_space(line[i]); i++) {
        /* scan */
    }
    if (i < len && line[i] == '#') return;

    /* rule 2: the first " # " and everything after it */
    for (i = 0; i + 2 < end; i++) {
        if (is_space(line[i]) && line[i + 1] == '#' && is_space(line[i + 2])) {
            end = i;
            break;
        }
    }
    /* rule 3: a trailing " #" */
    if (end >= 2 && line[end - 1] == '#' && is_space(line[end - 2])) end -= 2;

    /* rules 4 and 5: trim both ends */
    while (start < end && is_space(line[start])) start++;
    while (end > start && is_space(line[end - 1])) end--;
    str_add(out, line + start, end - start);
}

/* theme_lines -- every directive line of a manifest, already stripped and with
 * the empty ones dropped. Returns 0 when the file is not there (sh: `[ -f ]`). */
typedef struct {
    Str *items;
    size_t count;
    /* last_incomplete -- the manifest's final line had no newline AND survived
     * the strip. sed preserves that missing newline, and the two consumers of
     * this list then behave differently from every other line: a `while read`
     * loop never runs its body for it, and a printing sed emits it without a
     * trailing newline. Both show up in the generated substitution script. */
    int last_incomplete;
} Directives;

static int theme_lines(Directives *out, const char *path) {
    char *buf;
    size_t len;
    size_t pos = 0;
    Line line;
    Str stripped;
    size_t cap = 16;

    out->items = NULL;
    out->count = 0;
    out->last_incomplete = 0;
    buf = slurp(path, &len);
    if (buf == NULL) return 0;

    out->items = (Str *)calloc(cap, sizeof(Str));
    if (out->items == NULL) osr_die_oom();
    str_init(&stripped);
    while (next_line(buf, len, &pos, &line)) {
        strip_directive(&stripped, line.start, line.len);
        if (stripped.len == 0) continue;        /* /^$/d */
        if (out->count == cap) {
            Str *bigger;
            cap *= 2;
            bigger = (Str *)realloc(out->items, cap * sizeof(Str));
            if (bigger == NULL) osr_die_oom();
            out->items = bigger;
        }
        str_init(&out->items[out->count]);
        str_add(&out->items[out->count], str_text(&stripped), stripped.len);
        out->count++;
        out->last_incomplete = !line.had_newline;
    }
    str_free(&stripped);
    free(buf);
    return 1;
}

static void directives_free(Directives *d) {
    size_t i;
    for (i = 0; i < d->count; i++) str_free(&d->items[i]);
    free(d->items);
    d->items = NULL;
    d->count = 0;
}

/* osr_theme_read_lines -- a manifest's directive lines for a caller that would
 * have run `_osr_theme_lines <file> | while IFS= read -r`. One line per entry,
 * each newline-terminated, and the manifest's FINAL line dropped when the file
 * ended without a newline: `read` returns false on a partial line, so the
 * shell loop never ran its body for it either. */
void osr_theme_read_lines(Str *out, const char *path) {
    Directives d;
    size_t i;
    size_t n;

    if (!theme_lines(&d, path)) return;
    n = d.count;
    if (d.last_incomplete && n > 0) n--;
    for (i = 0; i < n; i++) {
        str_add(out, str_text(&d.items[i]), d.items[i].len);
        str_addc(out, '\n');
    }
    directives_free(&d);
}

/* match_prefix -- the sed `s|^<key>:[[:space:]]*||p` shape: does this directive
 * start with "<key>:"? If so, *value points at the value, whitespace skipped.
 *
 * Written out rather than compiled as a BRE. The pattern was a regular
 * expression only because the shell original reached it through sed, and it
 * uses no metacharacter beyond [[:space:]]* -- while <regex.h> is POSIX-only,
 * and this unit is one of the ones both cores compile. */
static int match_prefix(const char *directive, const char *key, const char **value) {
    size_t key_len = strlen(key);

    if (strncmp(directive, key, key_len) != 0) return 0;
    if (directive[key_len] != ':') return 0;
    directive += key_len + 1;
    while (is_space(*directive)) directive++;
    *value = directive;
    return 1;
}

/* match_color_role -- `s|^color:[[:space:]]*<role>[[:space:]][[:space:]]*||p`,
 * the other shape, and the reason the space after the role is REQUIRED rather
 * than optional: without it `accent` would also match the `accent_red` row.
 * Returns the value, or NULL when this directive is not that role's. */
static const char *match_color_role(const char *directive, const char *role) {
    const char *value;
    size_t role_len = strlen(role);

    if (!match_prefix(directive, "color", &value)) return NULL;
    if (strncmp(value, role, role_len) != 0) return NULL;
    value += role_len;
    if (!is_space(*value)) return NULL;
    while (is_space(*value)) value++;
    return value;
}

/* --- the readers ---------------------------------------------------------- */

/* cmd_list -- every dir under themes/ that has a theme.list. */
/* osr_theme_list -- every theme name, one per line. A theme is a directory
 * under themes/ that carries a theme.list; anything else in there is not one. */
void osr_theme_list(Str *out) {
    Str dir;

    str_init(&dir);
    str_addz(&dir, osr_root());
    str_addz(&dir, "/themes");
    /* "carries a theme.list" is the definition, not a convention: a stray
     * folder under themes/ is not a theme and is not offered as one. */
    osr_list_dir(out, str_text(&dir), "theme.list", NULL);
    str_free(&dir);
}

static int cmd_list(void) {
    Str out;
    str_init(&out);
    osr_theme_list(&out);
    out_flush(&out);
    str_free(&out);
    return 0;
}

static int theme_list_path(Str *out, const char *name) {
    path_of(out, "themes", name, "theme.list");
    return file_exists(str_text(out));
}

/* osr_theme_exists -- is <name> a real theme (a themes/<name>/theme.list)? */
int osr_theme_exists(const char *name) {
    Str p;
    int ok;
    str_init(&p);
    ok = (*name != '\0') && theme_list_path(&p, name);
    str_free(&p);
    return ok;
}

static int cmd_exists(const char *name) { return osr_theme_exists(name) ? 0 : 1; }

/* first_value -- the first directive matching "<key>:", value only. */
static int first_value(Str *out, const char *manifest, const char *key) {
    Directives d;
    size_t i;
    int found = 0;

    if (!theme_lines(&d, manifest)) return 0;
    for (i = 0; i < d.count && !found; i++) {
        const char *v;
        if (match_prefix(str_text(&d.items[i]), key, &v)) {
            str_addz(out, v);
            found = 1;
        }
    }
    directives_free(&d);
    return found;
}

/* osr_theme_meta -- the same lookup for a caller inside this process, where
 * the shell tier spent a fork on `osr theme meta`. Appends nothing when the
 * theme does not define the key, which is what `$(...)` handed sh as "". */
void osr_theme_meta(Str *out, const char *name, const char *key) {
    Str manifest;
    str_init(&manifest);
    path_of(&manifest, "themes", name, "theme.list");
    (void)first_value(out, str_text(&manifest), key);
    str_free(&manifest);
}

/* cmd_meta -- osr_theme_meta: a single-valued field, "" when absent. No
 * trailing newline: the sh version ended with `printf '%s'`. */
static int cmd_meta(const char *name, const char *key) {
    Str out;
    str_init(&out);
    osr_theme_meta(&out, name, key);
    out_flush(&out);
    str_free(&out);
    return 0;
}

/* split_words -- `tr ' ' '\n' | grep -v '^$'`: one word per line. */
static void split_words(Str *out, const char *value) {
    const char *p = value;
    while (*p != '\0') {
        const char *start;
        while (*p == ' ') p++;
        start = p;
        while (*p != '\0' && *p != ' ') p++;
        if (p > start) {
            str_add(out, start, (size_t)(p - start));
            str_addc(out, '\n');
        }
    }
}

/* cmd_configs -- every `config:` line (not just the first), space-split. */
/* osr_theme_configs -- the whole config/ dirs the theme drops into ~/.config on
 * apply (its `config:` lines), one per line. */
void osr_theme_configs(Str *out, const char *name) {
    Str manifest;
    Directives d;
    size_t i;

    str_init(&manifest);
    path_of(&manifest, "themes", name, "theme.list");
    if (theme_lines(&d, str_text(&manifest))) {
        for (i = 0; i < d.count; i++) {
            const char *v;
            if (match_prefix(str_text(&d.items[i]), "config", &v)) split_words(out, v);
        }
        directives_free(&d);
    }
    str_free(&manifest);
}

static int cmd_configs(const char *name) {
    Str out;
    str_init(&out);
    osr_theme_configs(&out, name);
    out_flush(&out);
    str_free(&out);
    return 0;
}

/* cmd_color -- osr_theme_color, whose sed was
 * `s|^color:[[:space:]]*<role>[[:space:]][[:space:]]*||p`,
 * first hit. The role must be followed by at least one space, which is what
 * keeps `accent` from matching `accent_red`. */
static int cmd_color(const char *name, const char *role) {
    Str manifest;
    Str out;
    Directives d;
    size_t i;
    int found = 0;

    str_init(&manifest);
    path_of(&manifest, "themes", name, "theme.list");
    str_init(&out);
    if (theme_lines(&d, str_text(&manifest))) {
        for (i = 0; i < d.count && !found; i++) {
            const char *value = match_color_role(str_text(&d.items[i]), role);
            if (value != NULL) {
                str_addz(&out, value);
                found = 1;
            }
        }
        directives_free(&d);
    }
    out_flush(&out);
    str_free(&out);
    str_free(&manifest);
    return 0;
}

/* hex_dec -- `#rrggbb` -> "r,g,b", the sh version's 0x arithmetic. */
static void hex_dec(Str *out, const char *hex, char sep) {
    const char *h = (*hex == '#') ? hex + 1 : hex;
    int i;
    for (i = 0; i < 3; i++) {
        char pair[3];
        pair[0] = h[i * 2] != '\0' ? h[i * 2] : '0';
        pair[1] = pair[0] != '\0' && h[i * 2] != '\0' ? h[i * 2 + 1] : '0';
        pair[2] = '\0';
        if (i > 0) str_addc(out, sep);
        str_addl(out, strtol(pair, NULL, 16));
    }
}

static int cmd_hex_dec(const char *hex) {
    Str out;
    str_init(&out);
    hex_dec(&out, hex, ',');
    out_flush(&out);
    str_free(&out);
    return 0;
}

/* is_hex6 -- exactly six hex digits and nothing else, the `\{6\}$` in the
 * sh version's first color pattern. */
static int is_hex6(const char *s) {
    int i;
    for (i = 0; i < 6; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return 0;
    }
    return s[6] == '\0';
}

/* split_color -- a `color: <role> <value>` directive into its two halves.
 * Returns 0 when the line is not one (no value after the role), which is the
 * case the sh script let fall through to its generic `key: value` rule. */
static int split_color(const char *directive, Str *role, Str *value) {
    const char *p = directive;
    const char *q;
    const char *start;
    if (strncmp(p, "color:", 6) != 0) return 0;
    p += 6;

    /* The sed patterns capture the role as `\([A-Za-z0-9_]*\)` followed by at
     * least one space -- a class that can also match NOTHING. So a role with a
     * character outside it ("NotAWord-role #123456") does not fail to match:
     * the engine backtracks to a zero-length role, and the whole rest of the
     * line becomes the value. That is why such a line ends up as `s|{{}}|...`
     * in the generated script, and it is reproduced here rather than
     * "corrected". */
    q = p;
    while (is_space(*q)) q++;
    start = q;
    while ((*q >= 'A' && *q <= 'Z') || (*q >= 'a' && *q <= 'z') ||
           (*q >= '0' && *q <= '9') || *q == '_') q++;
    if (q > start && is_space(*q)) {
        str_reset(role);
        str_add(role, start, (size_t)(q - start));
        while (is_space(*q)) q++;
        str_reset(value);
        str_addz(value, q);
        return 1;
    }
    if (is_space(*p)) {                          /* the zero-length role */
        q = p;
        while (is_space(*q)) q++;
        str_reset(role);
        str_reset(value);
        str_addz(value, q);
        return 1;
    }
    return 0;
}

/* generic_key -- `^\([a-z][a-z0-9_]*\):[[:space:]]*\(.*\)$`: the fallback rule
 * that turns any lowercase `key: value` into a {{key}} substitution. */
static int generic_key(const char *directive, Str *key, Str *value) {
    const char *p = directive;
    const char *start = p;
    if (!(*p >= 'a' && *p <= 'z')) return 0;
    p++;
    while ((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_') p++;
    if (*p != ':') return 0;
    str_reset(key);
    str_add(key, start, (size_t)(p - start));
    p++;
    while (is_space(*p)) p++;
    str_reset(value);
    str_addz(value, p);
    return 1;
}

/* cmd_sed -- _osr_theme_sed: the substitution script that makes a theme a
 * palette instead of a directory of app configs. Every color role gets four
 * spellings, because a color is written four ways across the configs os-rice
 * owns and a template must never hard-code one:
 *
 *   {{role}}      #rrggbb     GTK, Xresources, most TUIs
 *   {{role_rgb}}  rrggbb      foot, and anything CSS-adjacent that adds its own #
 *   {{role_dec}}  r,g,b       KDE color schemes, konsole, CSS rgba()
 *   {{role_sgr}}  r;g;b       ANSI truecolor escapes
 *
 * Order matters and is the sh version's: {{THEME}}, then every _dec/_sgr pair
 * (which needed arithmetic and so came from a separate pass), then one rule
 * per remaining directive.
 */
/* rules_add -- one {{key}} -> value substitution. */
static void rules_add(Rules *r, const char *key_open, const char *key_extra,
                      const char *value, size_t value_len) {
    Rule *bigger;
    Str from;
    Str to;

    if (r->count == r->cap) {
        r->cap = r->cap ? r->cap * 2 : 32;
        bigger = (Rule *)realloc(r->items, r->cap * sizeof(Rule));
        if (bigger == NULL) osr_die_oom();
        r->items = bigger;
    }
    str_init(&from);
    str_addz(&from, "{{");
    str_addz(&from, key_open);
    if (key_extra != NULL) str_addz(&from, key_extra);
    str_addz(&from, "}}");
    str_init(&to);
    str_add(&to, value, value_len);
    r->items[r->count].from = from;
    r->items[r->count].to = to;
    r->count++;
}

void osr_theme_rules_free(Rules *r) {
    size_t i;
    for (i = 0; i < r->count; i++) {
        str_free(&r->items[i].from);
        str_free(&r->items[i].to);
    }
    free(r->items);
    r->items = NULL;
    r->count = 0;
    r->cap = 0;
}

/* osr_theme_rules -- every {{key}} the theme defines, in the order the sh
 * version's sed script listed them: {{THEME}}, then the _dec/_sgr pair of
 * every hash color (which needed arithmetic and so came from a separate
 * pass), then one rule per remaining directive. *drop_final_newline reports
 * the trailing-newline quirk cmd_sed has to reproduce. */
void osr_theme_rules(Rules *out, const char *name, int *drop_final_newline) {
    Str manifest;
    Directives d;
    Str role;
    Str value;
    Str tmp;
    size_t i;

    out->items = NULL;
    out->count = 0;
    out->cap = 0;
    if (drop_final_newline != NULL) *drop_final_newline = 0;

    str_init(&manifest);
    path_of(&manifest, "themes", name, "theme.list");
    rules_add(out, "THEME", NULL, name, strlen(name));

    str_init(&role);
    str_init(&value);
    str_init(&tmp);
    if (theme_lines(&d, str_text(&manifest))) {
        for (i = 0; i < d.count; i++) {
            const char *v;
            if (i + 1 == d.count && d.last_incomplete) break; /* `while read` skips it */
            if (!split_color(str_text(&d.items[i]), &role, &value)) continue;
            v = str_text(&value);
            if (*v != '#' || !is_hex6(v + 1)) continue;
            str_reset(&tmp);
            hex_dec(&tmp, v, ',');
            rules_add(out, str_text(&role), "_dec", str_text(&tmp), tmp.len);
            str_reset(&tmp);
            hex_dec(&tmp, v, ';');
            rules_add(out, str_text(&role), "_sgr", str_text(&tmp), tmp.len);
        }
        for (i = 0; i < d.count; i++) {
            const char *directive = str_text(&d.items[i]);
            if (split_color(directive, &role, &value)) {
                const char *v = str_text(&value);
                if (*v == '#') {
                    rules_add(out, str_text(&role), NULL, v, strlen(v));
                    rules_add(out, str_text(&role), "_rgb", v + 1, strlen(v + 1));
                } else {
                    rules_add(out, str_text(&role), NULL, v, strlen(v));
                }
                continue;
            }
            if (strncmp(directive, "config:", 7) == 0) continue;   /* /^config:/d */
            if (generic_key(directive, &role, &value)) {
                rules_add(out, str_text(&role), NULL, str_text(&value), value.len);
            }
        }
        if (drop_final_newline != NULL) *drop_final_newline = d.last_incomplete;
        directives_free(&d);
    }
    str_free(&tmp);
    str_free(&role);
    str_free(&value);
    str_free(&manifest);
}

/* cmd_sed -- the rules as the sed script lib/config.sh fed to `sed -f`. The
 * C renderer (lib/render.c) applies the same rules directly; this stays
 * because the sh tier still renders with sed. */
static int cmd_sed(const char *name) {
    Str out;
    Rules rules;
    size_t i;
    int drop_final_newline = 0;

    osr_theme_rules(&rules, name, &drop_final_newline);
    str_init(&out);
    for (i = 0; i < rules.count; i++) {
        const char *from = str_text(&rules.items[i].from);
        str_addz(&out, "s|");
        str_addz(&out, from);
        str_addc(&out, '|');
        str_add(&out, str_text(&rules.items[i].to), rules.items[i].to.len);
        str_addz(&out, "|g\n");
    }
    osr_theme_rules_free(&rules);

    /* sed does not add a newline to a file that ended without one, so neither
     * does the rule generated from that last line. */
    if (drop_final_newline) str_trim_trailing(&out, '\n');
    out_flush(&out);
    str_free(&out);
    return 0;
}

/* cmd_session -- any | x11 | wayland, defaulting to any. */
static int cmd_session(const char *name) {
    Str manifest;
    Str out;
    str_init(&manifest);
    path_of(&manifest, "themes", name, "theme.list");
    str_init(&out);
    if (!first_value(&out, str_text(&manifest), "session") || out.len == 0) {
        str_reset(&out);
        str_addz(&out, "any");
    }
    out_flush(&out);
    str_free(&out);
    str_free(&manifest);
    return 0;
}

/* cmd_rice_themes -- the `themes:` line of a rice, falling back to its
 * `theme:` when it declares no set. */
static int cmd_rice_themes(const char *rice) {
    Str manifest;
    Str value;
    Str out;

    str_init(&manifest);
    path_of(&manifest, "rices", rice, "rice.list");
    str_init(&value);
    if (!first_value(&value, str_text(&manifest), "themes") || value.len == 0) {
        str_reset(&value);
        (void)first_value(&value, str_text(&manifest), "theme");
    }
    str_init(&out);
    split_words(&out, str_text(&value));
    out_flush(&out);
    str_free(&out);
    str_free(&value);
    str_free(&manifest);
    return 0;
}

/* osr_rice_default_theme -- a rice's `theme:` line: the theme installed with it.
 * Appends nothing when the manifest names none. */
void osr_rice_default_theme(Str *out, const char *rice) {
    Str manifest;
    str_init(&manifest);
    path_of(&manifest, "rices", rice, "rice.list");
    (void)first_value(out, str_text(&manifest), "theme");
    str_free(&manifest);
}

static int cmd_rice_default(const char *rice) {
    Str out;
    str_init(&out);
    osr_rice_default_theme(&out, rice);
    out_flush(&out);
    str_free(&out);
    return 0;
}

/* --- the picker ----------------------------------------------------------- */

/* SWATCH_ROLES -- `-` is a gap, not a role: it splits the theme's own
 * surface/text/accent colors from the fixed-meaning status trio. */
static const char *const swatch_roles[] = {
    "background", "surface", "text_muted", "foreground", "accent",
    "-", "success", "warning", "error"
};
#define SWATCH_ROLE_COUNT (sizeof(swatch_roles) / sizeof(swatch_roles[0]))

/* swatch -- colored blocks for a theme's palette. 48;2;r;g;b (truecolor) on
 * purpose, never the 0-15 palette indices: the point of the preview is to show
 * the theme's OWN colors, and an indexed color would be repainted by whatever
 * palette the terminal is currently wearing -- every theme would look the same. */
static void swatch(Str *out, const char *name) {
    Str manifest;
    Str value;
    size_t i;

    str_init(&manifest);
    path_of(&manifest, "themes", name, "theme.list");
    str_init(&value);
    for (i = 0; i < SWATCH_ROLE_COUNT; i++) {
        Directives d;
        size_t j;
        int found = 0;

        if (strcmp(swatch_roles[i], "-") == 0) {
            str_addz(out, "\033[0m  ");
            continue;
        }
        str_reset(&value);
        if (theme_lines(&d, str_text(&manifest))) {
            for (j = 0; j < d.count && !found; j++) {
                const char *v = match_color_role(str_text(&d.items[j]), swatch_roles[i]);
                if (v != NULL) {
                    str_addz(&value, v);
                    found = 1;
                }
            }
            directives_free(&d);
        }
        /* `case "$_sw_hex" in \#??????) ;; *) continue ;; esac` */
        if (!found || value.len != 7 || value.p[0] != '#') continue;
        str_addz(out, "\033[48;2;");
        hex_dec(out, str_text(&value), ';');
        str_addz(out, "m    ");
    }
    str_addz(out, "\033[0m");
    str_free(&value);
    str_free(&manifest);
}

static int cmd_swatch(const char *name) {
    Str out;
    str_init(&out);
    swatch(&out, name);
    out_flush(&out);
    str_free(&out);
    return 0;
}

/* cmd_menu -- the numbered picker. Prompt and input go through /dev/tty, never
 * stdout: this runs inside a `$(...)`, so stdout is the captured return value.
 * Empty, invalid or EOF input falls back to the default theme. */
/* osr_theme_menu -- the numbered picker. Prompt and input go through /dev/tty
 * because the shell tier called this inside a `$(...)`, where stdout IS the
 * return value; keeping the tty split makes the C caller identical. */
void osr_theme_menu(Str *out) {
    Str names;
    Str prompt;
    char **items = NULL;
    size_t count = 0;
    size_t cap = 0;
    size_t pos = 0;
    Line line;
    FILE *tty;
    FILE *tty_out;
    const char *dflt = env_str("OSR_DEFAULT_THEME", "xin");
    char answer[128];
    size_t i;

    /* the theme list, as `set -- $(osr_themes)` produced it */
    str_init(&names);
    osr_theme_list(&names);
    while (next_line(names.p != NULL ? names.p : "", names.len, &pos, &line)) {
        if (line.len == 0) continue;
        if (count == cap) {
            cap = cap ? cap * 2 : 8;
            items = (char **)realloc(items, cap * sizeof(char *));
            if (items == NULL) osr_die_oom();
        }
        items[count] = (char *)malloc(line.len + 1);
        if (items[count] == NULL) osr_die_oom();
        memcpy(items[count], line.start, line.len);
        items[count][line.len] = '\0';
        count++;
    }

    if (count == 0) {
        str_addz(out, dflt);
        str_free(&names);
        free(items);
        return;
    }

    tty = osr_tty_open(&tty_out);
    str_init(&prompt);
    str_addz(&prompt, "Select a theme:\n");
    for (i = 0; i < count; i++) {
        Str sw;
        char num[32];
        size_t pad;
        sprintf(num, "  %lu) ", (unsigned long)(i + 1));
        str_addz(&prompt, num);
        str_addz(&prompt, items[i]);
        for (pad = strlen(items[i]); pad < 12; pad++) str_addc(&prompt, ' '); /* %-12s */
        str_addc(&prompt, ' ');
        str_init(&sw);
        swatch(&sw, items[i]);
        str_add(&prompt, str_text(&sw), sw.len);
        str_addc(&prompt, '\n');
        str_free(&sw);
    }
    str_addz(&prompt, "Enter number [default ");
    str_addz(&prompt, dflt);
    str_addz(&prompt, "]: ");
    if (tty_out != NULL) {
        fwrite(str_text(&prompt), 1, prompt.len, tty_out);
        fflush(tty_out);
    }
    str_free(&prompt);

    answer[0] = '\0';
    if (tty == NULL || fgets(answer, (int)sizeof(answer), tty) == NULL) answer[0] = '\0';
    if (tty_out != NULL && tty_out != tty) fclose(tty_out);
    if (tty != NULL) fclose(tty);
    answer[strcspn(answer, "\r\n")] = '\0';

    {
        long pick = 0;
        int numeric = answer[0] != '\0';
        char *p;
        for (p = answer; *p != '\0'; p++) {
            if (*p < '0' || *p > '9') numeric = 0;      /* `*[!0-9]*` */
        }
        if (numeric) pick = strtol(answer, NULL, 10);
        if (numeric && pick >= 1 && pick <= (long)count) str_addz(out, items[pick - 1]);
        else str_addz(out, dflt);
    }
    str_free(&names);
    for (i = 0; i < count; i++) free(items[i]);
    free(items);
}

static int cmd_menu(void) {
    Str out;
    str_init(&out);
    osr_theme_menu(&out);
    out_flush(&out);
    str_free(&out);
    return 0;
}

/* --- the shell-callable half of lib/theme.sh, in process -------------------
 *
 * These are the three functions the sh shim existed for: they SET
 * OSR_THEME/OSR_THEME_DIR for everything downstream, end the run through
 * error(), and loop over apply_config. None of that needed a shell once the
 * caller stopped being one -- the environment they publish is inherited by every
 * child this process forks, which is exactly what `export` bought the shim.
 */

/* osr_resolve_theme -- set OSR_THEME + OSR_THEME_DIR. Resolution order:
 * explicit name > interactive menu > default theme. After this, a module's
 * `[ -f "$OSR_THEME_DIR/config/..." ]` guards fire. Fatal on a name that is not
 * a theme: that one the user typed, and guessing at it would paint the wrong
 * desktop silently. */
void osr_resolve_theme(const char *want) {
    Str pick, dir;

    str_init(&pick); str_init(&dir);
    if (want != NULL && *want != '\0') {
        if (!osr_theme_exists(want))
            osr_die("no such theme: '%s' (see: osr themes)", want);
        str_addz(&pick, want);
    } else if (osr_interactive()) {
        osr_theme_menu(&pick);
    } else {
        str_addz(&pick, env_str("OSR_DEFAULT_THEME", "xin"));
        osr_infof("no interactive terminal - using default theme '%s'", str_text(&pick));
    }

    str_addz(&dir, osr_root());
    str_addz(&dir, "/themes/");
    str_addz(&dir, str_text(&pick));
    osr_setenv("OSR_THEME", str_text(&pick));
    osr_setenv("OSR_THEME_DIR", str_text(&dir));
    osr_infof("theme: %s", str_text(&pick));
    str_free(&pick); str_free(&dir);
}

/* osr_unset_theme -- "this run paints nothing", explicitly.
 *
 * The counterpart to osr_resolve_theme for a module set where no module reads
 * the theme (osr_module_themable says no for all of them): every theme guard is
 * `[ -n "$OSR_THEME_DIR" ]`, so empty-and-exported is the value that makes them
 * all decline, and nothing is asked of the user for an answer nothing consumes. */
void osr_unset_theme(void) {
    osr_setenv("OSR_THEME", "");
    osr_setenv("OSR_THEME_DIR", "");
}

/* osr_apply_theme_configs -- drop the whole config/ dirs the current theme
 * declares (`config:` in theme.list) into ~/.config. These are the appearance
 * dirs no module owns; a module-owned layer is installed by its module. */
int osr_apply_theme_configs(void) {
    const char *theme = env_str("OSR_THEME", "");
    Str list;
    size_t pos = 0;
    Line line;
    int ok = 1;

    if (*theme == '\0') return 1;
    str_init(&list);
    osr_theme_configs(&list, theme);
    while (next_line(str_text(&list), list.len, &pos, &line)) {
        Str name;
        if (line.len == 0) continue;
        str_init(&name);
        str_add(&name, line.start, line.len);
        ok = osr_apply_config(str_text(&name)) && ok;
        str_free(&name);
    }
    str_free(&list);
    return ok;
}

static int usage(void) {
    fputs("usage: osr theme <subcommand> [args]\n\n", stderr);
    fputs("  list | exists <name> | session <name>\n", stderr);
    fputs("  lines <file>            the manifest parser's output\n", stderr);
    fputs("  meta <name> <key> | color <name> <role> | configs <name>\n", stderr);
    fputs("  hex-dec <#rrggbb> | sed <name> | swatch <name>\n", stderr);
    fputs("  rice-themes <rice> | rice-default <rice>\n", stderr);
    fputs("  menu                    the numbered picker, on /dev/tty\n", stderr);
    return 2;
}

int osr_theme_main(int argc, char **argv) {
    if (argc < 2) return usage();

    if (strcmp(argv[1], "list") == 0 && argc == 2) return cmd_list();
    if (strcmp(argv[1], "exists") == 0 && argc == 3) return cmd_exists(argv[2]);
    if (strcmp(argv[1], "lines") == 0 && argc == 3) {
        Directives d;
        Str out;
        size_t i;
        str_init(&out);
        if (theme_lines(&d, argv[2])) {
            for (i = 0; i < d.count; i++) {
                str_add(&out, str_text(&d.items[i]), d.items[i].len);
                str_addc(&out, '\n');
            }
            directives_free(&d);
        }
        out_flush(&out);
        str_free(&out);
        return 0;
    }
    if (strcmp(argv[1], "meta") == 0 && argc == 4) return cmd_meta(argv[2], argv[3]);
    if (strcmp(argv[1], "configs") == 0 && argc == 3) return cmd_configs(argv[2]);
    if (strcmp(argv[1], "color") == 0 && argc == 4) return cmd_color(argv[2], argv[3]);
    if (strcmp(argv[1], "hex-dec") == 0 && argc == 3) return cmd_hex_dec(argv[2]);
    if (strcmp(argv[1], "sed") == 0 && argc == 3) return cmd_sed(argv[2]);
    if (strcmp(argv[1], "session") == 0 && argc == 3) return cmd_session(argv[2]);
    if (strcmp(argv[1], "rice-themes") == 0 && argc == 3) return cmd_rice_themes(argv[2]);
    if (strcmp(argv[1], "rice-default") == 0 && argc == 3) return cmd_rice_default(argv[2]);
    if (strcmp(argv[1], "swatch") == 0 && argc == 3) return cmd_swatch(argv[2]);
    if (strcmp(argv[1], "menu") == 0 && argc == 2) return cmd_menu();
    return usage();
}
