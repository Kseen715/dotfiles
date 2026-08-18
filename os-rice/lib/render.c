/* lib/render.c -- see lib/render.h. C89 + POSIX. */
#define _POSIX_C_SOURCE 200809L

#include "render.h"
#include "module.h"

#include <unistd.h>

/* apply_rules -- every rule over the whole buffer, in order. lib/config.sh ran
 * `sed -f <script>`, which applies rule 1 to a line, then rule 2 to the
 * result, and so on; doing the same over the whole text is equivalent (no
 * pattern here spans a newline) and keeps the one property that matters: a
 * value containing a `{{key}}` is still rewritten by a later rule. */
static void apply_rules(Str *out, const char *text, size_t len, const Rules *rules) {
    Str cur;
    Str next;
    size_t i;

    str_init(&cur);
    str_add(&cur, text, len);
    for (i = 0; i < rules->count; i++) {
        const char *from = str_text(&rules->items[i].from);
        size_t flen = rules->items[i].from.len;
        const char *p = str_text(&cur);
        size_t remaining = cur.len;

        if (flen == 0) continue;
        str_init(&next);
        while (remaining >= flen) {
            if (memcmp(p, from, flen) == 0) {
                str_add(&next, str_text(&rules->items[i].to), rules->items[i].to.len);
                p += flen;
                remaining -= flen;
            } else {
                str_addc(&next, *p);
                p++;
                remaining--;
            }
        }
        str_add(&next, p, remaining);
        str_free(&cur);
        cur = next;
    }
    str_add(out, str_text(&cur), cur.len);
    str_free(&cur);
}

/* unfilled -- the `{{key}}` names the theme did not define, exactly as the sh
 * version found them:
 *
 *     grep -v WALLPAPER_PATH <rendered> | sed -n <capture the {{name}}> | sort -u
 *
 * Three quirks to reproduce, all from that pipeline: the WALLPAPER_PATH filter
 * drops the whole LINE (so another placeholder sharing that line is missed
 * too), sed's leading `.*` is greedy so only the LAST placeholder on a line is
 * reported, and the result is sorted and deduplicated with a trailing space.
 */
static int name_cmp(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static void unfilled(Str *out, const char *text, size_t len) {
    size_t pos = 0;
    Line line;
    char **names = NULL;
    size_t count = 0;
    size_t cap = 0;
    size_t i;

    while (next_line(text, len, &pos, &line)) {
        Str l;
        const char *p;
        const char *last_start = NULL;
        size_t last_len = 0;

        str_init(&l);
        str_add(&l, line.start, line.len);
        p = str_text(&l);
        if (strstr(p, "WALLPAPER_PATH") != NULL) { str_free(&l); continue; } /* grep -v */
        while ((p = strstr(p, "{{")) != NULL) {
            const char *name = p + 2;
            const char *q = name;
            while ((*q >= 'A' && *q <= 'Z') || (*q >= 'a' && *q <= 'z') ||
                   (*q >= '0' && *q <= '9') || *q == '_') q++;
            if (q[0] == '}' && q[1] == '}' && q > name) {
                last_start = name;                 /* greedy `.*`: keep the last */
                last_len = (size_t)(q - name);
            }
            p += 2;
        }
        if (last_start != NULL) {
            if (count == cap) {
                cap = cap ? cap * 2 : 8;
                names = (char **)realloc(names, cap * sizeof(char *));
                if (names == NULL) osr_die_oom();
            }
            names[count] = (char *)malloc(last_len + 1);
            if (names[count] == NULL) osr_die_oom();
            memcpy(names[count], last_start, last_len);
            names[count][last_len] = '\0';
            count++;
        }
        str_free(&l);
    }

    if (count > 1) qsort(names, count, sizeof(char *), name_cmp);   /* sort -u */
    for (i = 0; i < count; i++) {
        if (i == 0 || strcmp(names[i], names[i - 1]) != 0) {
            str_addz(out, names[i]);
            str_addc(out, ' ');
        }
        free(names[i]);
    }
    free(names);
}

int osr_render_template(const char *src, const char *dst, const char *theme) {
    char *buf;
    size_t len;
    Str out;
    Rules rules;
    FILE *fp;

    buf = slurp(src, &len);
    if (buf == NULL) {
        osr_warnf("render_theme_template: template not found: %s", src);
        return 0;
    }
    if (*theme == '\0') {
        free(buf);
        osr_warnf("render_theme_template: no theme resolved");
        return 0;
    }

    osr_theme_rules(&rules, theme, NULL);
    str_init(&out);
    apply_rules(&out, buf, len, &rules);
    osr_theme_rules_free(&rules);
    free(buf);

    fp = fopen(dst, "wb");
    if (fp == NULL) {
        osr_warnf("render_theme_template: cannot write %s", dst);
        str_free(&out);
        return 0;
    }
    if (out.len > 0) fwrite(str_text(&out), 1, out.len, fp);
    fclose(fp);

    /* {{WALLPAPER_PATH}} is deliberately left: it is filled by the wallpaper
     * layer in a second pass, because the value is an installed path rather
     * than anything the theme's palette knows (§6). */
    {
        Str left;
        str_init(&left);
        unfilled(&left, str_text(&out), out.len);
        if (left.len > 0) {
            Str base;
            str_init(&base);
            base_of(&base, src);
            osr_warnf("theme '%s' defines no %s- left unsubstituted in %s",
                      theme, str_text(&left), str_text(&base));
            str_free(&base);
        }
        str_free(&left);
    }
    str_free(&out);
    return 1;
}

int osr_theme_source(Str *out, const char *app, const char *name, int *is_temp) {
    Str path;
    const char *theme_dir = osr_mod_theme_dir();
    const char *theme = osr_mod_theme();

    *is_temp = 0;
    /* The theme's own file wins: it is already this theme's version. */
    if (*theme_dir != '\0') {
        str_init(&path);
        str_addz(&path, theme_dir);
        str_addz(&path, "/config/");
        str_addz(&path, app);
        str_addc(&path, '/');
        str_addz(&path, name);
        if (file_exists(str_text(&path))) {
            str_add(out, str_text(&path), path.len);
            str_free(&path);
            return 1;
        }
        str_free(&path);
    }

    /* Otherwise the app's one template, painted with this theme's palette. */
    str_init(&path);
    str_addz(&path, osr_mod_dotfiles());
    str_addc(&path, '/');
    str_addz(&path, app);
    str_addc(&path, '/');
    str_addz(&path, name);
    str_addz(&path, ".tmpl");
    if (!file_exists(str_text(&path)) || *theme == '\0') {
        str_free(&path);
        return 0;
    }
    {
        Str tmp;
        Str base;
        int ok;
        str_init(&tmp);
        str_addz(&tmp, env_str("TMPDIR", "/tmp"));
        str_addz(&tmp, "/osr-theme-");
        str_addz(&tmp, app);
        str_addc(&tmp, '-');
        str_addl(&tmp, (long)getpid());
        str_addc(&tmp, '-');
        str_init(&base);
        base_of(&base, name);
        str_add(&tmp, str_text(&base), base.len);
        str_free(&base);
        ok = osr_render_template(str_text(&path), str_text(&tmp), theme);
        str_free(&path);
        if (!ok) { str_free(&tmp); return 0; }
        str_add(out, str_text(&tmp), tmp.len);
        str_free(&tmp);
        *is_temp = 1;
        return 1;
    }
}
