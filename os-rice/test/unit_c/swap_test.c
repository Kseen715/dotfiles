/* test/unit_c/swap_test.c -- how modules/swap.c sizes swap for the machine it
 * finds itself on.
 *
 * Swap is the one module whose output is a NUMBER rather than a package list,
 * and the number is derived from four facts that all vary: how much RAM, what
 * swap already exists, how much disk is free, and which init can set up zram.
 * Get it wrong in one direction and hibernation silently stops working; in the
 * other, a laptop's SSD loses 64 GiB to a swapfile nothing will ever page into.
 *
 * THE POLICY, WHICH IS WHAT THESE SCENARIOS SPELL OUT
 *
 *   zram covers RAM in full up to 8 GiB, then half of RAM up to an 8 GiB
 *   ceiling, and stops entirely at 24 GiB -- past there a compressed swap
 *   device competes with the page cache it is meant to relieve.
 *
 *   Disk swap targets RAM exactly (so a hibernation image fits) until RAM
 *   passes the point where hibernating is not a thing anyone does, and then
 *   drops to a flat 16 GiB overflow reserve.
 *
 *   Free disk overrides both. Half the free space, floored to a whole GiB,
 *   with an absolute reserve underneath it -- because a swapfile that fills
 *   the disk is worse than no swapfile.
 *
 * Hermetic: /proc/meminfo, /proc/swaps, fstab, the zram configs and the
 * swapfile itself are all fixture paths the module lets the caller name; df is
 * a stub; sudo logs every escalation and executes only the harmless writes, so
 * no real mkswap, swapon or systemctl ever runs.
 *
 * Replaces test/unit/swap_sizing.sh. See test/harness.h.
 */
#include "../harness.c"

static OsrSandbox sb;

/* --- the plan ---------------------------------------------------------
 *
 * The module publishes its sizing on one line:
 *
 *   swap: ram=8192M | zram want=8192M have=0M | disk want=8192M
 *         have partition=0M file=0M free=200000M -> swapfile 8192M
 *
 * which is the only place the numbers are observable -- everything else the
 * module does is a consequence of them. plan_field pulls one out.
 */
static long plan_field(const char *after) {
    const char *out = osr_sb_capture_both(&sb);
    const char *line = strstr(out, "swap: ram=");
    const char *at;
    if (line == NULL) return -1;
    at = strstr(line, after);
    if (at == NULL) return -1;
    at += strlen(after);
    while (*at == ' ') at++;
    if (*at < '0' || *at > '9') return -1;
    return atol(at);
}

static void sized(const char *after, long expected, const char *label) {
    long got = plan_field(after);
    HStr d;
    if (got == expected) { osr_ok(label); return; }
    hs_init(&d);
    hs_add(&d, "expected ");
    hs_addn(&d, expected);
    hs_add(&d, "M, got ");
    hs_addn(&d, got);
    hs_add(&d, "M");
    osr_fail(label, hs_text(&d));
    hs_free(&d);
}

/* file_holds / file_lacks -- a substring of one fixture file the module wrote. */
static void file_holds(const char *rel, const char *needle, const char *label) {
    HStr path;
    char *got;
    hs_init(&path);
    hs_path(&path, hs_text(&sb.root), rel);
    got = h_slurp(hs_text(&path));
    osr_assert_true(strstr(got, needle) != NULL, label);
    free(got);
    hs_free(&path);
}
static void file_lacks(const char *rel, const char *needle, const char *label) {
    HStr path;
    char *got;
    hs_init(&path);
    hs_path(&path, hs_text(&sb.root), rel);
    got = h_slurp(hs_text(&path));
    osr_assert_true(strstr(got, needle) == NULL, label);
    free(got);
    hs_free(&path);
}

static void said(const char *needle, const char *label) {
    osr_assert_true(strstr(osr_sb_capture_both(&sb), needle) != NULL, label);
}
static void quiet_about(const char *needle, const char *label) {
    osr_assert_true(strstr(osr_sb_capture_both(&sb), needle) == NULL, label);
}

/* fixture -- a box with `ram_mib` of RAM and `free_mib` free on /, and
 * `swaps` as the body of /proc/swaps (its header is supplied). */
static void fixture(long ram_mib, long free_mib, const char *swaps) {
    HStr mem, sw, free_s;

    hs_init(&mem);
    hs_add(&mem, "MemTotal:       ");
    hs_addn(&mem, ram_mib * 1024);
    hs_add(&mem, " kB\n");
    osr_sb_write(&sb, "meminfo", hs_text(&mem), 0644);
    hs_free(&mem);

    hs_init(&sw);
    hs_add(&sw, "Filename\t\t\t\tType\t\tSize\t\tUsed\t\tPriority\n");
    if (swaps != NULL) hs_add(&sw, swaps);
    osr_sb_write(&sb, "swaps", hs_text(&sw), 0644);
    hs_free(&sw);

    hs_init(&free_s);
    hs_addn(&free_s, free_mib);
    osr_sb_env(&sb, "FREE_MIB", hs_text(&free_s));
    hs_free(&free_s);

    osr_sb_write(&sb, "fstab", "", 0644);
    osr_sb_rm(&sb, "zram.conf");
    osr_sb_rm(&sb, "sysctl.conf");
    osr_sb_rm(&sb, "zramen.conf");
    osr_sb_rm(&sb, "swapfile");
    osr_sb_reset(&sb);
}

/* run -- the module under one init, in one virtualisation. */
static void run(const char *init, const char *virt) {
    osr_sb_env(&sb, "OSR_INIT", init);
    osr_sb_env(&sb, "OSR_VIRT", virt != NULL ? virt : "none");
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "module", "run", "swap", (const char *)NULL);
}

/* plan -- run under an init with NO zram setter.
 *
 * Deliberate: it keeps the sizing scenarios from touching a real zram setter,
 * and it means zram is UNREACHABLE in those cases -- so the swappiness they
 * produce is the no-zram value. The reachable inits are asserted separately. */
static void plan(void) { run("none", NULL); }

int main(void) {
    HStr p;

    osr_sb_init(&sb);
    hs_init(&p);

    osr_sb_env(&sb, "OSR_PKG", "pacman");
    osr_sb_env(&sb, "OSR_DISTRO", "arch");
    osr_sb_env(&sb, "OSR_ID_LIKE", "");
    osr_sb_env(&sb, "OSR_CODENAME", "");
    osr_sb_env(&sb, "OSR_VERSION_ID", "");

    /* Every path the module writes to, rebased into the sandbox. */
    {
        static const struct { const char *var; const char *rel; } knobs[] = {
            { "OSR_MEMINFO",     "meminfo" },
            { "OSR_PROC_SWAPS",  "swaps" },
            { "OSR_FSTAB",       "fstab" },
            { "OSR_ZRAM_CONF",   "zram.conf" },
            { "OSR_SYSCTL_CONF", "sysctl.conf" },
            { "OSR_SWAPFILE",    "swapfile" },
            { "OSR_ZRAMEN_CONF", "zramen.conf" }
        };
        size_t i;
        for (i = 0; i < sizeof(knobs) / sizeof(knobs[0]); i++) {
            hs_path(&p, hs_text(&sb.root), knobs[i].rel);
            osr_sb_env(&sb, knobs[i].var, hs_text(&p));
        }
    }

    /* sudo logs every escalation and executes ONLY the file writes: the real
     * mkswap, swapon and systemctl must never run against the machine running
     * the suite. */
    osr_sb_stub_body(&sb, "sudo",
        "printf 'sudo %s\\n' \"$*\" >>\"$LOG\"\n"
        "case \"$1\" in tee|mkdir|rm|cp) exec \"$@\" ;; esac\n"
        "exit 0\n");
    osr_sb_stub_body(&sb, "df",
        "printf 'Filesystem 1024-blocks Used Available Capacity Mounted\\n"
        "/dev/sda1 %s %s %s 50%% /\\n' \"$((FREE_MIB * 2048))\" "
        "\"$((FREE_MIB * 1024))\" \"$((FREE_MIB * 1024))\"\n");
    /* Nothing is installed, so every install is attempted and shows up in the
     * escalation log; pacman itself never runs. */
    osr_sb_stub_body(&sb, "pacman", "[ \"$1\" = \"-Q\" ] && exit 1\nexit 0\n");

    /* ================================================================
     * 1. The RAM tiers
     * ================================================================ */
    fixture(8192, 200000, NULL);
    plan();
    sized("zram want=", 8192, "8G RAM: zram covers RAM in full");
    sized("disk want=", 8192, "8G RAM: disk swap targets RAM, so hibernation fits");
    said("no zram setter for init=none",
        "an init with no zram setter says so rather than silently skipping");
    file_holds("sysctl.conf", "vm.swappiness = 10",
        "zram wanted but unreachable: swappiness stays low, because the swap "
        "that remains is on disk");

    fixture(4096, 200000, NULL);
    plan();
    sized("zram want=", 4096, "4G RAM: zram covers RAM in full");

    fixture(16384, 200000, NULL);
    plan();
    sized("zram want=", 8192, "16G RAM: zram is min(RAM/2, 8G)");
    sized("disk want=", 16384, "16G RAM: disk swap still targets RAM");

    /* At and above the threshold zram is off entirely: past this much RAM a
     * compressed swap device competes with the page cache it exists to
     * relieve, and suspend-to-disk stays cleaner without it. */
    fixture(24576, 400000, NULL);
    plan();
    sized("zram want=", 0, "24G RAM: no zram at all");
    sized("disk want=", 16384,
        "24G RAM: past the hibernation ceiling, the disk target drops to the "
        "flat overflow reserve");
    file_holds("sysctl.conf", "vm.swappiness = 10", "no zram: swappiness 10");

    fixture(32768, 400000, NULL);
    plan();
    sized("zram want=", 0, "32G RAM: no zram");
    sized("disk want=", 16384, "32G RAM: overflow reserve, not RAM-sized");

    fixture(65536, 900000, NULL);
    plan();
    sized("disk want=", 16384,
        "64G RAM: still the overflow reserve -- nobody hibernates a 64G box");

    fixture(262144, 2000000, NULL);
    plan();
    sized("disk want=", 16384,
        "256G RAM: 16G, not 256G of swapfile on a server's root disk");
    said("no hibernation", "and it says hibernation is off the table");

    /* MemTotal is always somewhat under the installed size, because firmware
     * and the kernel reserve some. So the tiers key off the ROUNDED-UP value:
     * a 24G box that reports 23.4G must not sneak back into the zram tier. */
    fixture(23400, 400000, NULL);
    plan();
    sized("zram want=", 0, "a 24G stick reporting 23.4G is still a 24G box: no zram");
    sized("disk want=", 16384, "and it takes the overflow reserve");

    fixture(15600, 400000, NULL);
    plan();
    sized("zram want=", 8192, "a 16G stick reporting 15.6G is still a 16G box");
    sized("disk want=", 16384,
        "and gets the full 16G, so its hibernation image fits");

    /* ================================================================
     * 2. Swap that is already there
     * ================================================================ */
    fixture(16384, 200000,
        "/dev/sda2                               partition\t4194304\t0\t-2\n");
    plan();
    sized("have partition=", 4096, "an existing swap partition is detected");
    sized("swapfile ", 12288,
        "and counted in full: the swapfile covers only the remainder");

    fixture(16384, 200000,
        "/dev/sda2                               partition\t20971520\t0\t-2\n");
    plan();
    sized("swapfile ", 0, "a partition bigger than the target needs no swapfile");
    said("no swapfile needed", "and the module says so");

    /* zram is swap, but it is not DISK swap: it cannot hold a hibernation
     * image and it disappears on reboot. Counting it against the disk target
     * would leave a box that cannot hibernate believing it can. */
    fixture(16384, 200000,
        "/dev/zram0                              partition\t8388608\t0\t100\n");
    plan();
    sized("have=", 8192, "an existing zram device is counted as zram");
    sized("have partition=", 0, "and NOT as a swap partition");
    sized("swapfile ", 16384, "so it does not reduce the disk target at all");

    /* ================================================================
     * 3. Free disk caps everything
     * ================================================================ */
    fixture(32768, 7000, NULL);
    plan();
    sized("swapfile ", 3072,
        "the swapfile is capped to half the free space, floored to a whole GiB");
    said("capping", "and the cap is reported rather than applied silently");

    fixture(32768, 1000, NULL);
    plan();
    sized("swapfile ", 0,
        "with under a gigabyte spendable, no swapfile is created at all");

    /* On a small disk the absolute reserve bites before the ratio does. */
    fixture(2048, 4096, NULL);
    plan();
    sized("swapfile ", 2048, "2G RAM on 4G free: a 2G swapfile, 2G left over");

    fixture(2048, 3000, NULL);
    plan();
    sized("swapfile ", 0, "2G RAM on 3G free: nothing to spare, no swapfile");

    /* The file being REPLACED is not competing for space with itself. */
    {
        HStr sw;
        hs_init(&sw);
        hs_path(&p, hs_text(&sb.root), "swapfile");
        hs_add(&sw, hs_text(&p));
        hs_add(&sw, "\tfile\t8388608\t0\t-2\n");
        fixture(32768, 100, hs_text(&sw));
        hs_free(&sw);
    }
    plan();
    sized("swapfile ", 4096,
        "the blocks of the existing swapfile count as free space -- it is "
        "being replaced, not added to");

    /* ================================================================
     * 4. What actually happens, on systemd
     * ================================================================ */
    fixture(16384, 200000, NULL);
    run("systemd", NULL);
    osr_assert_log(&sb, "zram-generator",
        "systemd: zram-generator is the setter, and it is installed");
    osr_assert_log(&sb, "sudo systemctl restart systemd-zram-setup@zram0",
        "systemd: the zram unit is restarted so the new size takes effect");
    file_holds("zram.conf", "zram-size = 8192",
        "systemd: the computed size reaches the zram config");
    file_holds("zram.conf", "swap-priority = 100",
        "systemd: zram outranks the swapfile, so it is used first");
    said("Creating", "the swapfile is created");
    file_holds("fstab", "none swap defaults,pri=10",
        "the fstab entry gives the swapfile a LOWER priority than zram");
    file_holds("sysctl.conf", "vm.swappiness = 100",
        "with zram active, swappiness goes high -- paging to RAM is cheap");
    file_holds("sysctl.conf", "vm.page-cluster = 0",
        "and page-cluster goes to zero: readahead into a compressed device "
        "costs more than it saves");
    said("hibernation fits", "16G RAM with 16G of disk swap: hibernation fits");

    /* ================================================================
     * 5. runit -- zram without systemd
     *
     * zram is a kernel feature, so an init without systemd is not an init
     * without zram. Void ships zramen as a runit service, and the POLICY has
     * to come out the same even though the mechanism is different.
     * ================================================================ */
    fixture(8192, 200000, NULL);
    run("runit", NULL);
    said("Installing zramen", "runit: zramen is the setter, not zram-generator");
    file_holds("sysctl.conf", "vm.swappiness = 100",
        "runit: zram is reachable, so it gets the zram swappiness");
    file_holds("sysctl.conf", "vm.page-cluster = 0", "runit: and page-cluster 0");
    file_holds("zramen.conf", "ZRAM_MAX_SIZE=8192",
        "runit: the computed ceiling reaches zramen");
    file_holds("zramen.conf", "ZRAM_SIZE=100",
        "8G RAM: zramen is asked for 100% of RAM");
    file_holds("zramen.conf", "ZRAM_PRIORITY=100",
        "runit: zram outranks the swapfile here too");

    /* The percentage has to track the tier rather than always saying 100. */
    fixture(16384, 200000, NULL);
    run("runit", NULL);
    file_holds("zramen.conf", "ZRAM_SIZE=50",
        "16G RAM: zramen is asked for half of RAM");
    file_holds("zramen.conf", "ZRAM_MAX_SIZE=8192",
        "16G RAM: and capped at the 8G ceiling");

    /* ================================================================
     * 6. The second run
     * ================================================================ */
    fixture(16384, 200000, NULL);
    run("systemd", NULL);
    {
        /* What the first run left behind, as /proc/swaps would now report it. */
        HStr sw;
        hs_init(&sw);
        hs_add(&sw, "Filename\t\t\t\tType\t\tSize\t\tUsed\t\tPriority\n");
        hs_path(&p, hs_text(&sb.root), "swapfile");
        hs_add(&sw, hs_text(&p));
        hs_add(&sw, "\tfile\t16777216\t0\t-2\n");
        hs_add(&sw, "/dev/zram0\tpartition\t8388608\t0\t100\n");
        osr_sb_write(&sb, "swaps", hs_text(&sw), 0644);
        hs_free(&sw);
    }
    run("systemd", NULL);
    said("zram already active", "a second run does not restart zram");
    quiet_about("Creating ", "a second run does not rewrite the swapfile");
    said("vm.swappiness already 100", "a second run does not rewrite the sysctl");
    {
        /* The fstab entry must not accumulate: a duplicate line is how a box
         * ends up trying to swapon the same file twice at boot. */
        HStr path;
        char *fstab;
        const char *at;
        int n = 0;
        hs_init(&path);
        hs_path(&path, hs_text(&sb.root), "fstab");
        fstab = h_slurp(hs_text(&path));
        at = fstab;
        while ((at = strstr(at, "none swap")) != NULL) { n++; at++; }
        osr_assert_true(n == 1, "the fstab entry is not duplicated on a rerun");
        free(fstab);
        hs_free(&path);
    }

    /* ================================================================
     * 7. A big-RAM box tears an old zram setup DOWN
     *
     * Someone who adds RAM to a 16G box should end up with the 32G policy, not
     * with the 16G one plus a note. This is the only place the module removes
     * something, and it is why the tier boundary is a real transition rather
     * than just a different number for new installs.
     * ================================================================ */
    fixture(32768, 400000,
        "/dev/zram0                              partition\t8388608\t0\t100\n");
    osr_sb_write(&sb, "zram.conf", "[zram0]\nzram-size = 8192\n", 0644);
    run("systemd", NULL);
    quiet_about("Installing zram-generator",
        "32G RAM: zram-generator is not installed");
    said("Disabling zram", "32G RAM: an existing zram setup is torn down");
    osr_assert_log(&sb, "sudo swapoff /dev/zram0",
        "32G RAM: and the live zram device is swapped off");
    file_holds("sysctl.conf", "vm.swappiness = 10",
        "32G RAM: swappiness drops back to the disk-swap value");
    file_lacks("sysctl.conf", "page-cluster",
        "32G RAM: page-cluster is left alone -- it is a zram tuning, and "
        "forcing it to 0 for disk swap would disable readahead");

    /* ================================================================
     * 8. A container owns no memory
     * ================================================================ */
    fixture(16384, 200000, NULL);
    run("systemd", "docker");
    quiet_about("Installing",
        "in a container the module does nothing: swap belongs to the host, and "
        "a guest cannot configure it");

    hs_free(&p);
    osr_sb_free(&sb);
    return osr_finish();
}
