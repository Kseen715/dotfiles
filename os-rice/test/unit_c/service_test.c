/* test/unit_c/service_test.c -- what lib/service.c must do to an init.
 *
 * Two idempotent verbs over four inits, plus the servicemap lookup that turns
 * a logical name into whatever this init calls it. No module ever runs
 * `systemctl` itself, so everything a rice can do to a box's services goes
 * through the commands asserted here.
 *
 * Hermetic: $PATH is a directory of stubs, so systemctl, rc-update, service
 * and sudo are scenario-controlled and no real init is touched. The init tools
 * answer from marker files -- ENABLED and ACTIVE say what `is-enabled` and
 * `is-active` report, which is the only state a scenario has to set to
 * describe an init's opinion.
 *
 * Each scenario asserts the COMPLETE command list rather than a substring:
 * what a service verb did to the box is the whole of what it ran, so an extra
 * command is as much a defect as a missing one. The doubled lines are the
 * sandbox working -- the sudo stub logs the escalation and then execs the real
 * command, which is itself a stub and logs again, so both that it escalated
 * and what it escalated to are visible.
 *
 * Was test/unit/service_c_parity.sh, which diffed this against lib/service.sh.
 * The expectations are stated here now; see test/harness.h for why.
 */
#include "../harness.c"

static OsrSandbox sb;

/* fresh -- empty the three directories a scenario plays in, so nothing a
 * previous one left can be mistaken for something this one did. */
static void fresh(void) {
    osr_sb_rm(&sb, "state");
    osr_sb_rm(&sb, "sv");
    osr_sb_rm(&sb, "service");
    osr_sb_mkdir(&sb, "state");
    osr_sb_mkdir(&sb, "sv");
    osr_sb_mkdir(&sb, "service");
    osr_sb_reset(&sb);
}

/* on <init> -- which init the next scenarios run against. */
static void on(const char *init) {
    osr_sb_env(&sb, "OSR_INIT", init);
}

/* run -- `osr service <verb> <name>`, after clearing the log. */
static int run(const char *verb, const char *name) {
    fresh();
    return osr_sb_run_core(&sb, "service", verb, name, (const char *)NULL);
}

/* init_tools -- one stub per init front end. */
static void init_tools(void) {
    static const char *tools[] = {
        "systemctl", "rc-update", "rc-service", "update-rc.d", "service", NULL
    };
    int i;
    for (i = 0; tools[i] != NULL; i++) {
        HStr body;
        hs_init(&body);
        hs_add(&body, "printf '");
        hs_add(&body, tools[i]);
        hs_add(&body, " %s\\n' \"$*\" >>\"$LOG\"\n"
                      "case \"$1\" in\n"
                      "    is-enabled) [ -f \"$STATE/ENABLED\" ] || exit 1 ;;\n"
                      "    is-active)  [ -f \"$STATE/ACTIVE\" ]  || exit 1 ;;\n"
                      "esac\n"
                      "exit 0\n");
        osr_sb_stub_body(&sb, tools[i], hs_text(&body));
        hs_free(&body);
    }
}

/* --- 1. servicemap resolution -------------------------------------------
 *
 * `resolve` is a pure lookup: it prints a name and runs nothing. The rows are
 * qualified by init, so the same logical name answers differently per init --
 * that is the entire reason the map exists, and asserting the systemd case
 * alongside the runit one is what proves the qualifier is being read rather
 * than the first matching row taken.
 */
static void resolution(void) {
    /* resolve prints the bare name with NO trailing newline: it exists to be
     * captured by a `$(...)`, which would strip one anyway. */
    on("runit");
    run("resolve", "bluetooth");
    osr_assert_out_is(&sb, "bluetoothd", "runit: bluetooth is bluetoothd");
    osr_assert_log_empty(&sb, "runit: resolving runs no command");

    run("resolve", "cups");
    osr_assert_out_is(&sb, "cupsd", "runit: cups is cupsd");

    run("resolve", "smb");
    osr_assert_out_is(&sb, "smbd", "runit: smb is smbd");

    run("resolve", "sshd");
    osr_assert_out_is(&sb, "sshd", "runit: an unlisted name is left alone");

    on("systemd");
    run("resolve", "bluetooth");
    osr_assert_out_is(&sb, "bluetooth",
                      "systemd: the runit rows do not apply");

    /* The map's own filename appears in it as a comment. A parser that took
     * comment text for a row would answer this with something else. */
    run("resolve", "servicemap");
    osr_assert_out_is(&sb, "servicemap", "a comment row is not a service");
}

/* --- 2. systemd ----------------------------------------------------------
 *
 * enable = enable + start now, disable = stop + disable, both idempotent. The
 * idempotence is the point: a rice is applied repeatedly, so the second run
 * must do strictly less than the first, and "already enabled AND already
 * running" must do nothing at all.
 */
static void systemd(void) {
    on("systemd");

    /* `enable --now` rather than enable-then-start: one command does both,
     * so there is no window in which the unit is enabled but not running. */
    run("enable", "bluetooth");
    osr_assert_log_is(&sb,
        "systemctl is-enabled bluetooth\n"
        "sudo systemctl enable --now bluetooth\n"
        "systemctl enable --now bluetooth\n",
        "systemd: a disabled unit is enabled and started in one command");

    /* Disable asks only whether it is enabled -- an already-disabled unit is
     * left alone whether or not something else started it. */
    run("disable", "bluetooth");
    osr_assert_log_is(&sb,
        "systemctl is-enabled bluetooth\n",
        "systemd: an already-disabled unit is not touched");

    /* Enabled but not running: skip the enable, still start it. */
    fresh();
    osr_sb_write(&sb, "state/ENABLED", "", 0644);
    osr_sb_run_core(&sb, "service", "enable", "bluetooth", (const char *)NULL);
    osr_assert_log_is(&sb,
        "systemctl is-enabled bluetooth\n"
        "systemctl is-active bluetooth\n"
        "sudo systemctl enable --now bluetooth\n"
        "systemctl enable --now bluetooth\n",
        "systemd: enabled but stopped still gets --now, to start it");

    fresh();
    osr_sb_write(&sb, "state/ENABLED", "", 0644);
    osr_sb_run_core(&sb, "service", "disable", "bluetooth", (const char *)NULL);
    osr_assert_log_is(&sb,
        "systemctl is-enabled bluetooth\n"
        "sudo systemctl disable --now bluetooth\n"
        "systemctl disable --now bluetooth\n",
        "systemd: an enabled unit is disabled and stopped in one command");

    /* Enabled and running: the whole verb is a no-op past the two probes.
     * This is the idempotence contract in one assertion. */
    fresh();
    osr_sb_write(&sb, "state/ENABLED", "", 0644);
    osr_sb_write(&sb, "state/ACTIVE", "", 0644);
    osr_sb_run_core(&sb, "service", "enable", "bluetooth", (const char *)NULL);
    osr_assert_log_is(&sb,
        "systemctl is-enabled bluetooth\n"
        "systemctl is-active bluetooth\n",
        "systemd: enabled and running asks twice and acts not at all");
}

/* --- 3. openrc, sysvinit, and an init nobody knows ----------------------- */
static void other_inits(void) {
    on("openrc");
    run("enable", "cups");
    osr_assert_log_is(&sb,
        "sudo rc-update add cups default\n"
        "rc-update add cups default\n"
        "sudo rc-service cups start\n"
        "rc-service cups start\n",
        "openrc: adds to the default runlevel, then starts");

    run("disable", "cups");
    osr_assert_log_is(&sb,
        "sudo rc-service cups stop\n"
        "rc-service cups stop\n"
        "sudo rc-update del cups default\n"
        "rc-update del cups default\n",
        "openrc: stops before removing from the runlevel");

    on("sysvinit");
    run("enable", "cups");
    osr_assert_log_is(&sb,
        "sudo update-rc.d cups enable\n"
        "update-rc.d cups enable\n"
        "sudo service cups start\n"
        "service cups start\n",
        "sysvinit: update-rc.d enable, then start");

    run("disable", "cups");
    osr_assert_log_is(&sb,
        "sudo service cups stop\n"
        "service cups stop\n"
        "sudo update-rc.d cups disable\n"
        "update-rc.d cups disable\n",
        "sysvinit: stops before disabling");

    /* An init this build has never heard of must WARN and do nothing -- not
     * guess at a command, and not fail the run. A rice that reaches an
     * unknown init has a service it cannot enable, which is worth saying and
     * is not worth aborting an otherwise good install over. */
    on("upstart");
    run("enable", "cups");
    osr_assert_log_empty(&sb, "an unknown init runs no command on enable");
    osr_assert_err(&sb, "[WARN]", "an unknown init warns on enable");

    run("disable", "cups");
    osr_assert_log_empty(&sb, "an unknown init runs no command on disable");
    osr_assert_err(&sb, "[WARN]", "an unknown init warns on disable");
}

/* --- 4. runit ------------------------------------------------------------
 *
 * The odd one out, and the reason the tests assert the filesystem as well as
 * the command list: enabling under runit is one symlink. Assert only the argv
 * log and a broken implementation that ran `ln -s` with the wrong target
 * would still look right.
 */
static void runit(void) {
    on("runit");

    /* The package shipped /etc/sv/<name>: link it into the service dir. */
    fresh();
    osr_sb_mkdir(&sb, "sv/bluetoothd");
    osr_sb_run_core(&sb, "service", "enable", "bluetooth", (const char *)NULL);
    osr_assert_log_is(&sb,
        "sudo ln -s ROOT/sv/bluetoothd ROOT/service/bluetoothd\n",
        "runit: one link, and nothing else");
    osr_assert_link(&sb, "service/bluetoothd", "ROOT/sv/bluetoothd",
                    "runit: the link points at the shipped service");

    /* Nothing ships one: warn, and leave the service dir empty. Silently
     * linking a non-existent directory would leave runit supervising nothing. */
    fresh();
    osr_sb_run_core(&sb, "service", "enable", "bluetooth", (const char *)NULL);
    osr_assert_log_empty(&sb, "runit: no service shipped, nothing run");
    osr_assert_err(&sb, "package ships no runit service",
                   "runit: and it says why");
    osr_assert_tree_is(&sb, "service", "service\n",
                       "runit: no link is left behind");

    /* Already linked: idempotent, so the second apply must not relink. */
    fresh();
    osr_sb_mkdir(&sb, "sv/bluetoothd");
    osr_sb_symlink(&sb, "sv/bluetoothd", "service/bluetoothd");
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "service", "enable", "bluetooth", (const char *)NULL);
    osr_assert_log_empty(&sb, "runit: an existing link is left alone");
    osr_assert_link(&sb, "service/bluetoothd", "ROOT/sv/bluetoothd",
                    "runit: and it still points where it did");

    fresh();
    osr_sb_mkdir(&sb, "sv/bluetoothd");
    osr_sb_symlink(&sb, "sv/bluetoothd", "service/bluetoothd");
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "service", "disable", "bluetooth", (const char *)NULL);
    osr_assert_log_is(&sb, "sudo rm -f ROOT/service/bluetoothd\n",
                      "runit: disable removes the link");
    osr_assert_absent(&sb, "service/bluetoothd",
                      "runit: and the link is really gone");

    /* Disabling something never enabled: also a no-op, not an error. */
    fresh();
    osr_sb_run_core(&sb, "service", "disable", "bluetooth", (const char *)NULL);
    osr_assert_log_empty(&sb, "runit: disabling an unlinked service does nothing");
}

int main(void) {
    HStr p;

    osr_sb_init(&sb);
    init_tools();
    osr_sb_real(&sb, "ls");

    hs_init(&p);
    hs_path(&p, hs_text(&sb.root), "state");
    osr_sb_env(&sb, "STATE", hs_text(&p));
    hs_path(&p, hs_text(&sb.root), "sv");
    osr_sb_env(&sb, "OSR_SV_DIR", hs_text(&p));
    hs_path(&p, hs_text(&sb.root), "service");
    osr_sb_env(&sb, "OSR_SERVICE_DIR", hs_text(&p));
    hs_free(&p);

    /* Void with xbps: the distro whose init is runit, the branch with the
     * most to get wrong. The others are selected per scenario by OSR_INIT. */
    osr_sb_env(&sb, "OSR_DISTRO", "void");
    osr_sb_env(&sb, "OSR_PKG", "xbps");
    osr_sb_env(&sb, "HOME", hs_text(&sb.root));
    osr_sb_env(&sb, "OSR_HOME", hs_text(&sb.root));

    resolution();
    systemd();
    other_inits();
    runit();

    osr_sb_free(&sb);
    return osr_finish();
}
