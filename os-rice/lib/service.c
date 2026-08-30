/* lib/service.c -- C port of lib/service.sh. See lib/service.h.
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "service.h"
#include "cmds.h"
#include "module.h"

/* map_line_value -- if line is `<key> = <value>`, append the value to out and
 * return 1. Leading space is skipped, a trailing ` #comment` is dropped, and
 * both ends of the value are trimmed -- the sed the sh version ran on the
 * text after the `=`. */
static int map_line_value(Str *out, const Line *line, const char *key) {
    const char *p = line->start;
    const char *end = line->start + line->len;
    size_t key_len = strlen(key);
    const char *val;
    const char *val_end;
    const char *hash;

    while (p < end && is_space(*p)) p++;
    if ((size_t)(end - p) < key_len || strncmp(p, key, key_len) != 0) return 0;
    p += key_len;
    while (p < end && is_space(*p)) p++;
    if (p >= end || *p != '=') return 0;
    p++;

    /* `s/[[:space:]]#.*$//` -- a # counts as a comment only when spaced off,
     * so a value may legitimately contain one. */
    val = p;
    val_end = end;
    for (hash = val; hash + 1 < end; hash++) {
        if (is_space(*hash) && hash[1] == '#') { val_end = hash; break; }
    }
    while (val < val_end && is_space(*val)) val++;
    while (val_end > val && is_space(val_end[-1])) val_end--;
    str_add(out, val, (size_t)(val_end - val));
    return 1;
}

void osr_service_resolve(Str *out, const char *name) {
    Str path;
    Str key;
    char *buf;
    size_t len;
    int pass;
    int found = 0;

    str_init(&path);
    str_addz(&path, env_str("OSR_LIB", "lib"));
    str_addz(&path, "/servicemap");
    buf = slurp(str_text(&path), &len);
    str_free(&path);
    if (buf == NULL) { str_addz(out, name); return; }

    str_init(&key);
    /* Most specific first: `<name>@<init>`, then the bare `<name>`. */
    for (pass = 0; pass < 2 && !found; pass++) {
        size_t pos = 0;
        Line line;
        str_reset(&key);
        str_addz(&key, name);
        if (pass == 0) {
            str_addc(&key, '@');
            str_addz(&key, env_str("OSR_INIT", ""));
        }
        while (next_line(buf, len, &pos, &line)) {
            /* head -n 1: the first matching row wins. */
            if (map_line_value(out, &line, str_text(&key))) { found = 1; break; }
        }
    }
    str_free(&key);
    free(buf);
    if (!found) str_addz(out, name);
}

int osr_service_enable(const char *name) {
    if (osr_theme_only()) return osr_theme_only_skip("enable_service");
    Str svc;
    const char *init = env_str("OSR_INIT", "");
    char *argv[6];
    int rc = 1;

    str_init(&svc);
    osr_service_resolve(&svc, name);

    if (strcmp(init, "systemd") == 0) {
        char *chk[4];
        chk[0] = (char *)"systemctl"; chk[1] = (char *)"is-enabled";
        chk[2] = (char *)str_text(&svc); chk[3] = NULL;
        if (osr_run_quiet(chk) == 0) {
            chk[1] = (char *)"is-active";
            if (osr_run_quiet(chk) == 0) {
                osr_infof("%s already enabled + running - skipping", str_text(&svc));
                str_free(&svc);
                return 1;
            }
        }
        argv[0] = (char *)"systemctl"; argv[1] = (char *)"enable";
        argv[2] = (char *)"--now"; argv[3] = (char *)str_text(&svc); argv[4] = NULL;
        rc = osr_run_root(argv) == 0;
    } else if (strcmp(init, "openrc") == 0) {
        argv[0] = (char *)"rc-update"; argv[1] = (char *)"add";
        argv[2] = (char *)str_text(&svc); argv[3] = (char *)"default"; argv[4] = NULL;
        (void)osr_run_root(argv);
        argv[0] = (char *)"rc-service"; argv[1] = (char *)str_text(&svc);
        argv[2] = (char *)"start"; argv[3] = NULL;
        rc = osr_run_root(argv) == 0;
    } else if (strcmp(init, "runit") == 0) {
        Str sv;
        Str run;
        str_init(&sv);
        str_addz(&sv, env_str("OSR_SV_DIR", "/etc/sv"));
        str_addc(&sv, '/');
        str_addz(&sv, str_text(&svc));
        str_init(&run);
        str_addz(&run, env_str("OSR_SERVICE_DIR", "/var/service"));
        str_addc(&run, '/');
        str_addz(&run, str_text(&svc));
        /* ln -s succeeds even when the target is missing, so an unpackaged
         * service would silently leave a dangling link that runsvdir then
         * complains about forever. Check first and degrade to a warning. */
        if (!dir_exists(str_text(&sv))) {
            osr_warnf("no %s - skipping (package ships no runit service)", str_text(&sv));
        } else if (!file_exists(str_text(&run)) && !dir_exists(str_text(&run))) {
            argv[0] = (char *)"ln"; argv[1] = (char *)"-s";
            argv[2] = (char *)str_text(&sv); argv[3] = (char *)str_text(&run);
            argv[4] = NULL;
            rc = osr_run_root(argv) == 0;
        }
        str_free(&sv);
        str_free(&run);
    } else if (strcmp(init, "sysvinit") == 0) {
        argv[0] = (char *)"update-rc.d"; argv[1] = (char *)str_text(&svc);
        argv[2] = (char *)"enable"; argv[3] = NULL;
        (void)osr_run_root(argv);
        argv[0] = (char *)"service"; argv[1] = (char *)str_text(&svc);
        argv[2] = (char *)"start"; argv[3] = NULL;
        rc = osr_run_root(argv) == 0;
    } else {
        osr_warnf("enable_service: unknown init '%s' - skipping %s", init, str_text(&svc));
    }
    str_free(&svc);
    return rc;
}

int osr_service_disable(const char *name) {
    if (osr_theme_only()) return osr_theme_only_skip("disable_service");
    Str svc;
    const char *init = env_str("OSR_INIT", "");
    char *argv[6];
    int rc = 1;

    str_init(&svc);
    osr_service_resolve(&svc, name);

    if (strcmp(init, "systemd") == 0) {
        char *chk[4];
        chk[0] = (char *)"systemctl"; chk[1] = (char *)"is-enabled";
        chk[2] = (char *)str_text(&svc); chk[3] = NULL;
        if (osr_run_quiet(chk) != 0) {
            osr_infof("%s already disabled - skipping", str_text(&svc));
            str_free(&svc);
            return 1;
        }
        argv[0] = (char *)"systemctl"; argv[1] = (char *)"disable";
        argv[2] = (char *)"--now"; argv[3] = (char *)str_text(&svc); argv[4] = NULL;
        rc = osr_run_root(argv) == 0;
    } else if (strcmp(init, "openrc") == 0) {
        argv[0] = (char *)"rc-service"; argv[1] = (char *)str_text(&svc);
        argv[2] = (char *)"stop"; argv[3] = NULL;
        (void)osr_run_root(argv);
        argv[0] = (char *)"rc-update"; argv[1] = (char *)"del";
        argv[2] = (char *)str_text(&svc); argv[3] = (char *)"default"; argv[4] = NULL;
        rc = osr_run_root(argv) == 0;
    } else if (strcmp(init, "runit") == 0) {
        Str run;
        str_init(&run);
        str_addz(&run, env_str("OSR_SERVICE_DIR", "/var/service"));
        str_addc(&run, '/');
        str_addz(&run, str_text(&svc));
        if (file_exists(str_text(&run)) || dir_exists(str_text(&run))) {
            argv[0] = (char *)"rm"; argv[1] = (char *)"-f";
            argv[2] = (char *)str_text(&run); argv[3] = NULL;
            rc = osr_run_root(argv) == 0;
        } else {
            /* sh's `[ -e ] && as_root rm` left the test's own status behind,
             * which is a failure when the link is not there. */
            rc = 0;
        }
        str_free(&run);
    } else if (strcmp(init, "sysvinit") == 0) {
        argv[0] = (char *)"service"; argv[1] = (char *)str_text(&svc);
        argv[2] = (char *)"stop"; argv[3] = NULL;
        (void)osr_run_root(argv);
        argv[0] = (char *)"update-rc.d"; argv[1] = (char *)str_text(&svc);
        argv[2] = (char *)"disable"; argv[3] = NULL;
        rc = osr_run_root(argv) == 0;
    } else {
        osr_warnf("disable_service: unknown init '%s' - skipping %s", init, str_text(&svc));
    }
    str_free(&svc);
    return rc;
}

static int service_usage(void) {
    fputs("usage: osr service <subcommand> [args]\n\n", stderr);
    fputs("  resolve <name>   this init's real unit name\n", stderr);
    fputs("  enable <name>    enable + start now\n", stderr);
    fputs("  disable <name>   stop + disable\n", stderr);
    return 2;
}

int osr_service_main(int argc, char **argv) {
    if (argc < 2) return service_usage();

    if (strcmp(argv[1], "resolve") == 0 && argc == 3) {
        Str out;
        str_init(&out);
        osr_service_resolve(&out, argv[2]);
        /* No trailing newline: every caller read this through `$( )`. */
        fputs(str_text(&out), stdout);
        str_free(&out);
        return 0;
    }
    if (strcmp(argv[1], "enable") == 0 && argc == 3)
        return osr_service_enable(argv[2]) ? 0 : 1;
    if (strcmp(argv[1], "disable") == 0 && argc == 3)
        return osr_service_disable(argv[2]) ? 0 : 1;

    return service_usage();
}
