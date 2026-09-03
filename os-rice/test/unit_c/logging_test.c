/* test/unit_c/logging_test.c -- modules/logging.c, the module whose whole
 * purpose is to be there BEFORE the thing it documents happens.
 *
 * A stock Void box keeps no log a crash can be read out of afterwards: no
 * syslog daemon runs at all, and /var/log/dmesg.log describes the boot that
 * came after the reset, not the one that died. So "why did it lock up" is
 * unanswerable unless this ran first -- which is what makes the two service
 * names below a promise and not a detail.
 *
 * THE PROMISES
 *
 *   runit   installs socklog-void and enables BOTH readers. socklog-unix reads
 *           /dev/log; nanoklogd reads /dev/kmsg. Only the second one carries a
 *           GPU or ACPI failure, so enabling one is the same as enabling none
 *           for the case this module exists for.
 *   systemd creates /var/log/journal (journald's own switch from volatile to
 *           persistent storage) and states the intent in a drop-in.
 *   other   installs nothing over the syslog daemon the init already ships,
 *           and SAYS so -- a silent skip reads exactly like success.
 *
 * Hermetic: $PATH is stubs, so no package manager and no service manager runs.
 * See test/harness.h.
 */
#include "../harness.c"

static OsrSandbox sb;

static int run_on(const char *init) {
    osr_sb_env(&sb, "OSR_INIT", init);
    osr_sb_reset(&sb);
    return osr_sb_run_core(&sb, "module", "run", "logging", (const char *)NULL);
}

int main(void) {
    HStr p;

    osr_sb_init(&sb);
    hs_init(&p);

    /* runit enables a service by linking /etc/sv/<name> into /var/service, and
     * refuses to link one the package did not ship -- so the sandbox needs both
     * directories, with the two service dirs present. Those paths are the
     * scenario, not this machine's. */
    osr_sb_mkdir(&sb, "sv");
    osr_sb_mkdir(&sb, "sv/socklog-unix");
    osr_sb_mkdir(&sb, "sv/nanoklogd");
    osr_sb_mkdir(&sb, "service");
    hs_path(&p, hs_text(&sb.root), "sv");
    osr_sb_env(&sb, "OSR_SV_DIR", hs_text(&p));
    hs_path(&p, hs_text(&sb.root), "service");
    osr_sb_env(&sb, "OSR_SERVICE_DIR", hs_text(&p));

    osr_sb_env(&sb, "OSR_DISTRO", "void");
    osr_sb_env(&sb, "OSR_ID_LIKE", "");
    osr_sb_env(&sb, "OSR_PKG", "xbps");
    osr_sb_stub_body(&sb, "xbps-query", "exit 1\n");
    osr_sb_stub_body(&sb, "xbps-install",
        "printf 'xbps-install %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
    osr_sb_stub_body(&sb, "sv", "printf 'sv %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
    osr_sb_stub_body(&sb, "ln", "printf 'ln %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");

    /* ================================================================
     * 1. runit: the package and both readers
     * ================================================================ */
    run_on("runit");
    osr_assert_log(&sb, "socklog-void",
        "runit: the socklog package is installed -- Void runs no syslog daemon "
        "at all without it");
    osr_assert_log(&sb, "socklog-unix",
        "runit: the /dev/log reader is enabled");
    osr_assert_log(&sb, "nanoklogd",
        "runit: and the KERNEL ring reader -- the one that carries the GPU and "
        "ACPI lines a crash investigation needs");

    /* ================================================================
     * 2. systemd: journald is running, it is just not persistent
     * ================================================================ */
    osr_sb_env(&sb, "OSR_DISTRO", "ubuntu");
    osr_sb_env(&sb, "OSR_ID_LIKE", "debian");
    osr_sb_env(&sb, "OSR_PKG", "apt");
    osr_sb_stub_body(&sb, "systemctl",
        "printf 'systemctl %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
    run_on("systemd");
    osr_assert_log(&sb, "/var/log/journal",
        "systemd: the journal directory is created -- its existence IS the "
        "switch from volatile to persistent");
    osr_assert_log(&sb, "journald.conf.d",
        "systemd: and the intent is written down, so the directory does not "
        "look accidental to the next reader");
    osr_refute_log(&sb, "socklog",
        "systemd: no second syslog daemon is installed next to journald");

    /* ================================================================
     * 3. Any other init: say what was not done
     * ================================================================ */
    run_on("openrc");
    osr_assert_log_empty(&sb,
        "openrc: nothing is installed over the syslog daemon the init ships");
    osr_assert_true(strstr(osr_sb_capture_both(&sb), "openrc") != NULL,
        "openrc: and the skip names the init, rather than passing silently");

    hs_free(&p);
    osr_sb_free(&sb);
    return osr_finish();
}
