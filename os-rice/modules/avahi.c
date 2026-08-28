/* modules/avahi.c -- mDNS/zeroconf (i3-sugg §7.1). This is what makes
 * `.local` hostnames resolve, driverless network printers appear in CUPS, and
 * KDE Connect / LocalSend find peers at all.
 *
 * Two halves: the daemon (avahi) and the NSS plugin (nss-mdns). Installing the
 * daemon alone is the classic half-configuration — the printer shows up in the
 * CUPS web UI but nothing can resolve its name, because /etc/nsswitch.conf still
 * has no `mdns` entry. The edit below is idempotent and only touches the hosts:
 * line (§2).
 *
 * Port of modules/avahi.sh, kept as the reference at
 * test/ref/avahi_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>
#include <string.h>

int osrm_avahi(void) {
    static const char *const pkgs[] = { "avahi", "nss-mdns", NULL };
    char *buf;
    size_t len;
    int ok;

    ok = osr_pkg_install_step("Installing Avahi (mDNS)", pkgs);

    buf = slurp("/etc/nsswitch.conf", &len);
    if (buf != NULL) {
        Str out;
        size_t pos = 0;
        Line l;
        int already = 0, rewrote = 0;

        /* Find the hosts: line and see whether it already resolves mdns. */
        while (!already && next_line(buf, len, &pos, &l)) {
            if (l.len >= 6 && strncmp(l.start, "hosts:", 6) == 0) {
                Str line;
                str_init(&line);
                str_add(&line, l.start, l.len);
                already = strstr(str_text(&line), "mdns") != NULL;
                str_free(&line);
                break;
            }
        }
        if (already) {
            osr_info("/etc/nsswitch.conf already resolves mdns - skipping");
            free(buf);
            goto service;
        }
        osr_info("adding mdns4_minimal to the hosts: line in /etc/nsswitch.conf");
        {
            char *argv[5];
            argv[0] = (char *)"cp"; argv[1] = (char *)"-f";
            argv[2] = (char *)"/etc/nsswitch.conf";
            argv[3] = (char *)"/etc/nsswitch.conf.bak"; argv[4] = NULL;
            (void)osr_run_root(argv);
        }
        /* Standard upstream ordering: mdns before dns, with [NOTFOUND=return]
         * so a negative mDNS answer does not stall every lookup. */
        str_init(&out);
        pos = 0;
        while (next_line(buf, len, &pos, &l)) {
            if (!rewrote && l.len >= 6 && strncmp(l.start, "hosts:", 6) == 0) {
                const char *p = l.start + 6;
                const char *end = l.start + l.len;
                str_addz(&out, "hosts:");
                while (p < end && (*p == ' ' || *p == '\t')) str_addc(&out, *p++);
                str_addz(&out, "mdns4_minimal [NOTFOUND=return] ");
                str_add(&out, p, (size_t)(end - p));
                rewrote = 1;
            } else {
                str_add(&out, l.start, l.len);
            }
            str_addc(&out, '\n');
        }
        free(buf);
        /* Written beside the original and moved into place: a half-written
         * nsswitch.conf is a box that cannot resolve anything. The move is
         * unconditional, as it was in sh -- a move of a file that never got
         * written fails on its own and changes nothing, where making it
         * conditional would be a second, different decision. */
        (void)osr_write_root("/etc/nsswitch.conf.new", str_text(&out));
        {
            char *argv[4];
            argv[0] = (char *)"mv"; argv[1] = (char *)"/etc/nsswitch.conf.new";
            argv[2] = (char *)"/etc/nsswitch.conf"; argv[3] = NULL;
            (void)osr_run_root(argv);
        }
        str_free(&out);
    }
service:
    if (!osr_service_enable("avahi-daemon"))
        osr_warn("could not enable avahi-daemon (needs a real init)");
    return ok;
}
