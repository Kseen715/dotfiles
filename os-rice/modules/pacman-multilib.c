/* modules/pacman-multilib.c -- enable Arch's [multilib] repo (needed for 32-bit
 * packages: steam, lib32-*). ONE copy, POSIX (was .../modules/pacman-multilib.sh).
 * Arch-specific by nature; idempotent — the repo is added only once, then the
 * index refreshed. No-op on non-pacman hosts (the rice is Arch-only anyway).
 *
 * Was modules/pacman-multilib.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>
#include <string.h>

int osrm_pacman_multilib(void) {
    char *buf;
    size_t len;
    char *argv[5];
    int enabled = 0;

    if (strcmp(osr_mod_pkg(), "pacman") != 0) {
        osr_info("multilib is Arch-only - skipping");
        return 1;
    }
    buf = slurp("/etc/pacman.conf", &len);
    if (buf != NULL) {
        size_t pos = 0;
        Line l;
        while (!enabled && next_line(buf, len, &pos, &l))
            enabled = l.len == 10 && strncmp(l.start, "[multilib]", 10) == 0;
        free(buf);
    }
    if (enabled) {
        osr_info("[multilib] already enabled in /etc/pacman.conf - skipping");
        return 1;
    }
    /* A shell, deliberately: the append IS the mutation, and `sh -c` keeps it
     * one privileged command rather than a read, an edit and a write. */
    argv[0] = (char *)"sh"; argv[1] = (char *)"-c";
    argv[2] = (char *)"printf \"\\n[multilib]\\nInclude = /etc/pacman.d/mirrorlist\\n\" "
                      ">> /etc/pacman.conf";
    argv[3] = NULL;
    if (!osr_run_step_root("Enabling [multilib] repository", argv)) return 0;
    /* The index does not know about the repo that was just enabled. */
    osr_pkg_refresh();
    return 1;
}
