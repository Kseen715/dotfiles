/* lib/preflight.c -- C port of lib/preflight.sh. See lib/preflight.h.
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

#include <glob.h>
#include <stdio.h>
#include <string.h>

#include "module.h"

#include "cmds.h"
#include "preflight.h"

/* env_eq -- does the named detection variable hold exactly this value? An
 * unset variable is "", which is what sh compared against too. */
static int env_eq(const char *name, const char *value) {
    return strcmp(env_str(name, ""), value) == 0;
}

/* gpu_present -- a render node under OSR_DRI (which the tests override, the
 * same way OSR_DRM works in detect.sh), or a count detect.sh already took. */
static int gpu_present(void) {
    Str pattern;
    glob_t g;
    int found;

    if (env_long("OSR_GPU_COUNT", 0) > 0) return 1;

    str_init(&pattern);
    str_addz(&pattern, env_str("OSR_DRI", "/dev/dri"));
    str_addz(&pattern, "/renderD*");
    found = 0;
    if (glob(str_text(&pattern), 0, NULL, &g) == 0) {
        size_t i;
        for (i = 0; i < g.gl_pathc; i++) {
            if (file_exists(g.gl_pathv[i]) || dir_exists(g.gl_pathv[i])) { found = 1; break; }
        }
        globfree(&g);
    }
    str_free(&pattern);
    return found;
}

int osr_preflight_check(const char *predicate) {
    const char *colon = strchr(predicate, ':');
    const char *val = (colon != NULL) ? colon + 1 : "";

    /* Alternation: split the value on '|' and re-attach the tag, so each
     * branch is checked as a whole predicate (`distro:void`) rather than as a
     * bare value. A predicate with no '|' falls straight through. */
    if (colon != NULL && strchr(val, '|') != NULL) {
        Str branch;
        const char *p = val;
        int ok = 0;

        str_init(&branch);
        while (*p != '\0' && !ok) {
            const char *bar = strchr(p, '|');
            size_t n = (bar != NULL) ? (size_t)(bar - p) : strlen(p);
            if (n > 0) {
                str_reset(&branch);
                str_add(&branch, predicate, (size_t)(colon - predicate) + 1);
                str_add(&branch, p, n);
                ok = osr_preflight_check(str_text(&branch));
            }
            p = (bar != NULL) ? bar + 1 : p + n;
        }
        str_free(&branch);
        return ok;
    }

    if (strncmp(predicate, "arch:", 5) == 0)
        return env_eq("OSR_ARCH", val) || env_eq("OSR_ARCH_DEB", val);
    if (strncmp(predicate, "init:", 5) == 0)
        return env_eq("OSR_INIT", val);
    if (strncmp(predicate, "distro:", 7) == 0)
        return env_eq("OSR_DISTRO", val);
    if (strncmp(predicate, "release:", 8) == 0)
        return env_eq("OSR_CODENAME", val) || env_eq("OSR_VERSION_ID", val);
    if (strncmp(predicate, "cmd:", 4) == 0)
        return osr_have_cmd(val);
    if (strcmp(predicate, "gpu:present") == 0)
        return gpu_present();

    /* Unknown tag: warn and pass, so a manifest written against a newer
     * predicate set still installs on this build. */
    osr_warnf("unknown require predicate '%s' - ignoring", predicate);
    return 1;
}

void osr_preflight(const char *const preds[]) {
    size_t i;
    for (i = 0; preds[i] != NULL; i++) {
        if (preds[i][0] == '\0') continue;
        if (osr_preflight_check(preds[i])) {
            osr_infof("require %s - ok", preds[i]);
        } else {
            osr_die("rice needs '%s' (detected: arch=%s init=%s distro=%s gpu=%s)",
                    preds[i], env_str("OSR_ARCH", ""), env_str("OSR_INIT", ""),
                    env_str("OSR_DISTRO", ""), env_str("OSR_GPU_COUNT", "0"));
        }
    }
}

static int preflight_usage(void) {
    fputs("usage: osr preflight <predicate>...\n", stderr);
    fputs("       osr preflight check <predicate>   exit 0 when the host satisfies it\n", stderr);
    return 2;
}

int osr_preflight_main(int argc, char **argv) {
    if (argc < 2) return preflight_usage();

    if (strcmp(argv[1], "check") == 0 && argc == 3)
        return osr_preflight_check(argv[2]) ? 0 : 1;

    osr_preflight((const char *const *)&argv[1]);
    return 0;
}
