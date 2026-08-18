/* modules/docker.c -- Docker engine. Native-first: install the distro's
 * engine package (`docker.io` on Debian/Ubuntu, `moby-engine` on Fedora,
 * `docker` elsewhere - resolved by pkgmap), so it updates through the package
 * manager.
 *
 * Port of modules/docker.sh, kept as the reference at
 * test/ref/docker_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>
#include <string.h>

/* group_exists -- shadow's getent where it exists, /etc/group otherwise
 * (busybox images ship neither getent nor groupadd). */
static int group_exists(const char *group) {
    char *argv[4];
    char *buf;
    size_t len;
    int found = 0;

    if (osr_have_cmd("getent")) {
        argv[0] = (char *)"getent";
        argv[1] = (char *)"group";
        argv[2] = (char *)group;
        argv[3] = NULL;
        return osr_run_quiet(argv) == 0;
    }
    buf = slurp("/etc/group", &len);
    if (buf == NULL) return 0;
    {
        size_t pos = 0;
        Line line;
        size_t glen = strlen(group);
        while (!found && next_line(buf, len, &pos, &line)) {
            if (line.len > glen && strncmp(line.start, group, glen) == 0 &&
                line.start[glen] == ':') found = 1;
        }
    }
    free(buf);
    return found;
}

/* in_group -- `id -nG <user> | grep -qw docker`. */
static int in_group(const char *user, const char *group) {
    Str out;
    int found = 0;

    str_init(&out);
    {
        char *argv[5];
        argv[0] = (char *)"id";
        argv[1] = (char *)"-nG";
        argv[2] = (char *)user;
        argv[3] = NULL;
        if (osr_run_capture(argv, &out)) {
            const char *p = str_text(&out);
            size_t glen = strlen(group);
            while (*p != '\0' && !found) {
                const char *start = p;
                while (*p != '\0' && !is_space(*p)) p++;
                if ((size_t)(p - start) == glen && strncmp(start, group, glen) == 0) found = 1;
                while (*p != '\0' && is_space(*p)) p++;
            }
        }
    }
    str_free(&out);
    return found;
}

int osrm_docker(void) {
    static const char *const pkgs[] = { "docker", NULL };
    const char *user = osr_mod_user();
    char *argv[6];
    int ok;

    ok = osr_pkg_install_step("Installing Docker", pkgs);

    /* docker group + membership so OSR_USER can reach the socket without sudo.
     * root-for-root needs neither. Tool names differ (shadow's
     * groupadd/usermod vs busybox's addgroup), so dispatch on what exists. */
    if (!group_exists("docker")) {
        if (osr_have_cmd("groupadd")) {
            argv[0] = (char *)"groupadd"; argv[1] = (char *)"docker"; argv[2] = NULL;
            osr_run_step_root("Creating docker group", argv);
        } else if (osr_have_cmd("addgroup")) {
            argv[0] = (char *)"addgroup"; argv[1] = (char *)"docker"; argv[2] = NULL;
            osr_run_step_root("Creating docker group", argv);
        }
    }
    if (*user != '\0' && strcmp(user, "root") != 0) {
        if (in_group(user, "docker")) {
            osr_infof("%s already in docker group - skipping", user);
        } else if (osr_have_cmd("usermod")) {
            Str desc;
            str_init(&desc);
            str_addz(&desc, "Adding ");
            str_addz(&desc, user);
            str_addz(&desc, " to docker group");
            argv[0] = (char *)"usermod"; argv[1] = (char *)"-aG";
            argv[2] = (char *)"docker"; argv[3] = (char *)user; argv[4] = NULL;
            osr_run_step_root(str_text(&desc), argv);
            osr_warnf("docker group change takes effect on next login (or run: newgrp docker)");
            str_free(&desc);
        } else if (osr_have_cmd("addgroup")) {
            Str desc;
            str_init(&desc);
            str_addz(&desc, "Adding ");
            str_addz(&desc, user);
            str_addz(&desc, " to docker group");
            argv[0] = (char *)"addgroup"; argv[1] = (char *)user;
            argv[2] = (char *)"docker"; argv[3] = NULL;
            osr_run_step_root(str_text(&desc), argv);
            osr_warnf("docker group change takes effect on next login (or run: newgrp docker)");
            str_free(&desc);
        }
    }

    /* Enable the daemon where an init can run it. In a container (no real init
     * / cgroups) this cannot start dockerd, so degrade to a warning rather
     * than fail the run - the daemon is a real-init concern (§9), not an
     * install error. */
    if (!osr_service_enable("docker")) {
        osr_warnf("could not enable docker service (needs a real init)");
    }
    return ok;
}
