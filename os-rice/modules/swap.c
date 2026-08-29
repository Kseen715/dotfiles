/* modules/swap.c -- memory: zram first, disk swap only for the rest. ONE copy,
 * POSIX.
 *
 * Sizing, all MiB. Inputs are files/commands so the whole plan is mockable:
 * OSR_MEMINFO, OSR_PROC_SWAPS, OSR_SWAPFILE, OSR_FSTAB, OSR_ZRAM_CONF,
 * OSR_SYSCTL_CONF. The four knobs below are the only policy dials.
 *
 *   zram   RAM <  24G -> RAM <= 8G: cover RAM in full (it compresses ~2-3x, so
 *                        this costs far less than its nominal size and keeps a
 *                        small box off the disk entirely)
 *                        RAM >  8G: min(RAM/2, 8G) - past that the CPU cost of
 *                        compressing outweighs the pages saved
 *          RAM >= 24G -> NONE. There is nothing to rescue: the box is not going
 *                        to thrash, and zram's swap device would compete with
 *                        the disk swap that suspend-to-disk resumes from.
 *   disk   RAM <= 16G  -> RAM: sized so hibernation/hybrid-sleep can write the
 *                        whole image (needs `resume=` on the kernel cmdline too;
 *                        this module sizes the space, it does not edit the
 *                        bootloader)
 *          RAM >  16G  -> 16G: hibernating 24G+ means writing 24G+ on every
 *                        sleep - not worth the disk or the wait, so this is
 *                        plain overflow swap and hibernation is off the table
 *   priority    zram 100, disk swapfile 10 - both live at once, and the kernel
 *                        fills the higher priority first: pages go to RAM-speed
 *                        compressed swap and only spill to the disk when zram is
 *                        full. (A swap PARTITION keeps whatever priority its own
 *                        fstab line gives it; that line is the user's, not ours.)
 *   swappiness  zram present -> 100 + page-cluster=0 (compressed swap is cheap
 *                        and single-page, so lean on it - the disk stays out of
 *                        the way by priority, not by a timid swappiness)
 *               no zram      -> 10 (disk swap under a game/compile is stutter;
 *                        keep it for emergencies, not for routine reclaim)
 *
 * An existing swap PARTITION counts in full toward the disk target; only the
 * remainder becomes a swapfile on the root fs (the system drive, normally the
 * fastest one). That file is capped at half the free space there and rounded up
 * to whole GiB; a deficit under 1G buys nothing, so no file is made.
 *
 * Port of modules/swap.sh, kept as the reference at
 * test/ref/swap_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>
#include <unistd.h>

/* The measured machine plus the plan drawn from it -- pure arithmetic, so the
 * unit test can build one per fixture. */
typedef struct {
    long ram, ram_tier;
    long zram_want, zram_pct, zram_active;
    long disk_want, file_want;
    long have_zram, have_part, have_file;
    long free;
    long swappiness;
} Plan;

static const char *swapfile_path(void) { return env_str("OSR_SWAPFILE", "/swapfile"); }
static const char *meminfo_path(void)  { return env_str("OSR_MEMINFO", "/proc/meminfo"); }
static const char *swaps_path(void)    { return env_str("OSR_PROC_SWAPS", "/proc/swaps"); }
static const char *fstab_path(void)    { return env_str("OSR_FSTAB", "/etc/fstab"); }
static const char *zconf_path(void)    { return env_str("OSR_ZRAM_CONF", "/etc/systemd/zram-generator.conf"); }
static const char *zramen_path(void)   { return env_str("OSR_ZRAMEN_CONF", "/etc/sv/zramen/conf"); }
static const char *sysctl_path(void)   { return env_str("OSR_SYSCTL_CONF", "/etc/sysctl.d/99-osr-swap.conf"); }

static long file_prio(void) { return env_long("OSR_SWAPFILE_PRIO", 10); }

/* gib_up -- round a MiB figure up to a whole GiB. */
static long gib_up(long mib) { return (mib + 1023) / 1024 * 1024; }

/* dir_of_swapfile -- `dirname "$OSR_SWAPFILE"`. */
static void dir_of_swapfile(Str *out) {
    const char *p = swapfile_path();
    const char *slash = strrchr(p, '/');
    if (slash == NULL) { str_addc(out, '.'); return; }
    if (slash == p) { str_addc(out, '/'); return; }
    str_add(out, p, (size_t)(slash - p));
}

/* field -- the nth whitespace-separated field of a line, "" past the end. */
static void field(Str *out, const char *start, size_t len, int n) {
    size_t i = 0;
    int seen = 0;
    while (i < len) {
        size_t s;
        while (i < len && is_space(start[i])) i++;
        s = i;
        while (i < len && !is_space(start[i])) i++;
        if (i == s) break;
        if (++seen == n) { str_add(out, start + s, i - s); return; }
    }
}

/* mem_total -- `awk '/^MemTotal:/ { print int($2 / 1024); exit }'`. */
static long mem_total(const char *path) {
    char *buf;
    size_t len, pos = 0;
    Line line;
    long mib = 0;

    buf = slurp(path, &len);
    if (buf == NULL) return -1;
    while (next_line(buf, len, &pos, &line)) {
        if (line.len < 9 || strncmp(line.start, "MemTotal:", 9) != 0) continue;
        {
            Str v;
            str_init(&v);
            field(&v, line.start, line.len, 2);
            mib = atol(str_text(&v)) / 1024;
            str_free(&v);
        }
        break;
    }
    free(buf);
    return mib;
}

/* read_swaps -- what is active now. /proc/swaps sizes are KiB; zram shows up as
 * a "partition" named /dev/zramN, so match the name before the type. */
static void read_swaps(Plan *p) {
    char *buf;
    size_t len, pos = 0;
    Line line;
    int first = 1;

    p->have_zram = p->have_part = p->have_file = 0;
    buf = slurp(swaps_path(), &len);
    if (buf == NULL) return;
    while (next_line(buf, len, &pos, &line)) {
        Str name, type, size;
        long m;
        if (first) { first = 0; continue; }              /* awk's NR > 1 */
        str_init(&name); str_init(&type); str_init(&size);
        field(&name, line.start, line.len, 1);
        field(&type, line.start, line.len, 2);
        field(&size, line.start, line.len, 3);
        m = atol(str_text(&size)) / 1024;
        if (strncmp(str_text(&name), "/dev/zram", 9) == 0)      p->have_zram += m;
        else if (strcmp(str_text(&type), "partition") == 0)     p->have_part += m;
        else                                                    p->have_file += m;
        str_free(&name); str_free(&type); str_free(&size);
    }
    free(buf);
}

/* free_mib -- `df -Pk <dir> | awk 'NR == 2 { print int($4 / 1024) }'`. */
static long free_mib(const char *dir) {
    Str out;
    char *argv[4];
    long mib = 0;

    str_init(&out);
    argv[0] = (char *)"df"; argv[1] = (char *)"-Pk"; argv[2] = (char *)dir; argv[3] = NULL;
    if (osr_run_capture(argv, &out)) {
        size_t pos = 0;
        Line line;
        if (next_line(str_text(&out), out.len, &pos, &line)
            && next_line(str_text(&out), out.len, &pos, &line)) {
            Str v;
            str_init(&v);
            field(&v, line.start, line.len, 4);
            mib = atol(str_text(&v)) / 1024;
            str_free(&v);
        }
    }
    str_free(&out);
    return mib;
}

/* swap_plan -- read the machine and fill the plan. Pure measurement plus
 * arithmetic: it mutates nothing. */
static void swap_plan(Plan *p) {
    long zram_off_at = env_long("OSR_ZRAM_OFF_AT", 24576);
    long hib_max     = env_long("OSR_HIBERNATE_MAX_RAM", 16384);
    long disk_cap    = env_long("OSR_SWAP_MAX", 16384);
    long reserve     = env_long("OSR_SWAP_RESERVE", 2048);
    long need, avail, cap, room;
    int zram_reachable;
    Str dir;

    p->ram = mem_total(meminfo_path());
    if (p->ram < 0) osr_die("swap: cannot read %s", meminfo_path());
    if (p->ram <= 0) osr_die("swap: no MemTotal in %s", meminfo_path());

    /* Tier on the INSTALLED size, not MemTotal: the kernel/firmware reserve a
     * few hundred MiB, so a 24G machine reports ~23.4G and would fall a tier
     * short. RAM ships in 2GiB steps, so rounding up to the next 2GiB recovers
     * the number on the spec sheet without ever inventing a tier. */
    p->ram_tier = (p->ram + 2047) / 2048 * 2048;

    if (p->ram_tier >= zram_off_at)          p->zram_want = 0;
    else if (p->ram_tier <= 8192)            p->zram_want = p->ram_tier;
    else if (p->ram_tier / 2 > 8192)         p->zram_want = 8192;
    else                                     p->zram_want = p->ram_tier / 2;

    p->disk_want = (p->ram_tier <= hib_max) ? p->ram_tier : disk_cap;

    /* Whether zram will actually EXIST, which is not the same as wanting it.
     * zram is a kernel module and is init-agnostic; what is systemd-bound is
     * zram-generator, which IS a systemd generator. So each init gets its own
     * setter and only an init with neither falls back to no zram:
     *   systemd -> zram-generator      runit -> zramen (Void's, /etc/sv/zramen)
     * swappiness then follows what the machine ends up with rather than what it
     * asked for. */
    zram_reachable = strcmp(osr_mod_init(), "systemd") == 0
                  || strcmp(osr_mod_init(), "runit") == 0;
    p->zram_active = (p->zram_want > 0 && zram_reachable) ? 1 : 0;

    /* zramen takes a PERCENTAGE of RAM plus an absolute MB ceiling, so the same
     * policy has to be expressed both ways. The percentage carries the shape and
     * ZRAM_MAX_SIZE pins the 8G ceiling exactly, because a percentage of the
     * real MemTotal would drift off the tier. */
    p->zram_pct = (p->ram_tier > 0) ? p->zram_want * 100 / p->ram_tier : 0;
    if (p->zram_pct > 100) p->zram_pct = 100;

    /* zram is cheap and page-at-a-time; disk swap under load is stutter. */
    p->swappiness = p->zram_active ? 100 : 10;

    read_swaps(p);

    str_init(&dir);
    dir_of_swapfile(&dir);
    p->free = free_mib(str_text(&dir));

    need = p->disk_want - p->have_part;
    if (need < 0) need = 0;
    need = gib_up(need);
    /* The current swapfile is deleted before a new one is written, so its blocks
     * are free space too. Two limits, whichever bites first: never take more
     * than half of what is available (a ratio, for big disks), and always leave
     * the reserve behind (an absolute floor - half of a nearly-full disk is
     * still not enough room to survive an update). */
    avail = p->free + p->have_file;
    cap = avail / 2;
    room = avail - reserve;
    if (cap > room) cap = room;
    if (cap < 0) cap = 0;
    if (need > cap) {
        osr_warnf("swap: want %ldM of swapfile but only %ldM is spendable on %s - capping",
                  need, cap, str_text(&dir));
        need = cap / 1024 * 1024;
    }
    if (need < 1024) need = 0;
    p->file_want = need;
    str_free(&dir);
}

/* zram_conf -- the zram-generator drop-in for the computed size. A literal size,
 * not an expression, so the plan and the config can never disagree. */
static void zram_conf(Str *out, const Plan *p) {
    str_addz(out, "# managed by os-rice (modules/swap.sh)\n[zram0]\nzram-size = ");
    str_addl(out, p->zram_want);
    str_addz(out, "\ncompression-algorithm = zstd\nswap-priority = 100\n");
}

/* zramen_conf -- Void's zramen reads /etc/sv/zramen/conf as plain shell. Keys
 * are the ones the packaged conf documents (ZRAM_SIZE is a percentage,
 * ZRAM_MAX_SIZE an MB cap); zstd and priority 100 keep it in step with the
 * zram-generator drop-in so both inits land on the same policy. */
static void zramen_conf(Str *out, const Plan *p) {
    str_addz(out, "# managed by os-rice (modules/swap.sh)\n");
    str_addz(out, "export ZRAM_COMP_ALGORITHM=zstd\n");
    str_addz(out, "export ZRAM_PRIORITY=100\n");
    str_addz(out, "export ZRAM_SIZE=");
    str_addl(out, p->zram_pct);
    str_addz(out, "\nexport ZRAM_MAX_SIZE=");
    str_addl(out, p->zram_want);
    str_addc(out, '\n');
}

/* sysctl_conf -- how hard the kernel leans on whatever swap it ended up with. */
static void sysctl_conf(Str *out, const Plan *p) {
    str_addz(out, "# managed by os-rice (modules/swap.sh)\nvm.swappiness = ");
    str_addl(out, p->swappiness);
    str_addc(out, '\n');
    /* zram is single-page and cheap: reading ahead 8 pages per fault only wastes
     * decompression. Irrelevant (and unset) when swap is a disk - including the
     * case where zram was WANTED but the init could not provide it. */
    if (p->zram_active) str_addz(out, "vm.page-cluster = 0\n");
}

/* same_text -- `[ "$(cat file 2>/dev/null)" = "$(...)" ]`: a command
 * substitution drops trailing newlines on both sides. */
static int same_text(const char *path, const char *want) {
    char *buf;
    size_t len, wlen = strlen(want);
    int same;

    buf = slurp(path, &len);
    if (buf == NULL) return *want == '\0';
    while (len > 0 && buf[len - 1] == '\n') len--;
    while (wlen > 0 && want[wlen - 1] == '\n') wlen--;
    same = len == wlen && strncmp(buf, want, wlen) == 0;
    free(buf);
    return same;
}

static int apply_zramen(void *ctx) {
    const Plan *p = (const Plan *)ctx;
    Str body;
    char *argv[5];

    str_init(&body);
    zramen_conf(&body, p);
    (void)osr_write_root(zramen_path(), str_text(&body));
    str_free(&body);
    (void)osr_service_enable("zramen");
    /* The service only reads conf at start, so a resize needs a restart. `sv` is
     * a no-op-with-a-message when the service was only just linked. */
    argv[0] = (char *)"sv"; argv[1] = (char *)"restart"; argv[2] = (char *)"zramen";
    argv[3] = NULL;
    (void)osr_run_root_quiet(argv);
    return 1;
}

static int apply_zram(void *ctx) {
    const Plan *p = (const Plan *)ctx;
    Str body;
    char *argv[4];

    str_init(&body);
    zram_conf(&body, p);
    (void)osr_write_root(zconf_path(), str_text(&body));
    str_free(&body);
    argv[0] = (char *)"systemctl"; argv[1] = (char *)"daemon-reload"; argv[2] = NULL;
    (void)osr_run_root(argv);
    argv[0] = (char *)"systemctl"; argv[1] = (char *)"restart";
    argv[2] = (char *)"systemd-zram-setup@zram0.service"; argv[3] = NULL;
    (void)osr_run_root(argv);
    return 1;
}

/* disable_zram -- tear down zram we configured earlier (a RAM upgrade past the
 * threshold, or a retuned knob). Only ever removes OUR drop-in. */
static int disable_zram(void *ctx) {
    char *argv[4];
    (void)ctx;
    argv[0] = (char *)"rm"; argv[1] = (char *)"-f"; argv[2] = (char *)zconf_path();
    argv[3] = NULL;
    (void)osr_run_root(argv);
    argv[0] = (char *)"systemctl"; argv[1] = (char *)"daemon-reload"; argv[2] = NULL;
    (void)osr_run_root(argv);
    argv[0] = (char *)"swapoff"; argv[1] = (char *)"/dev/zram0"; argv[2] = NULL;
    (void)osr_run_root(argv);
    return 1;
}

static int apply_sysctl(void *ctx) {
    const Plan *p = (const Plan *)ctx;
    Str body, dir;
    char *argv[4];

    /* mkdir first: /etc/sysctl.d is NOT guaranteed to exist. Void ships none of
     * it by default - only /etc/sysctl.conf - and `tee` into a missing directory
     * fails with a bare "No such file or directory" that reads like a
     * permissions problem. The directory is still the right target: Void's runit
     * core-service 08-sysctl.sh globs /etc/sysctl.d/*.conf at boot, so a drop-in
     * here is applied on every start, which /etc/sysctl.conf edits would not be
     * (that file is the admin's, not ours). */
    str_init(&dir);
    {
        const char *path = sysctl_path();
        const char *slash = strrchr(path, '/');
        if (slash == NULL) str_addc(&dir, '.');
        else if (slash == path) str_addc(&dir, '/');
        else str_add(&dir, path, (size_t)(slash - path));
    }
    argv[0] = (char *)"mkdir"; argv[1] = (char *)"-p"; argv[2] = dir.p; argv[3] = NULL;
    (void)osr_run_root(argv);
    str_free(&dir);

    str_init(&body);
    sysctl_conf(&body, p);
    (void)osr_write_root(sysctl_path(), str_text(&body));
    str_free(&body);

    argv[0] = (char *)"sysctl"; argv[1] = (char *)"-p"; argv[2] = (char *)sysctl_path();
    argv[3] = NULL;
    (void)osr_run_root(argv);
    return 1;
}

/* make_file -- (re)create the swapfile at file_want MiB and enable it. The whole
 * body is one `as_root sh -c`, as in the sh module: it needs a lock fd, an
 * `exec 9>`, and a fallback chain, none of which survives being split into
 * separate escalations. */
static int make_file(void *ctx) {
    const Plan *p = (const Plan *)ctx;
    const char *swapfile = swapfile_path();
    Str s, prio;
    char *argv[4];
    int rc;

    str_init(&s); str_init(&prio);
    str_addl(&prio, file_prio());
    str_addz(&s,
        "\n"
        "        set -e\n"
        "\n"
        "        # A lock, because the fast path below was once the slow path: when this\n"
        "        # step appears to hang, the natural thing to do is run the module again,\n"
        "        # and two concurrent 4 GiB writes to the same file corrupt both.\n"
        "        exec 9>'");
    str_addz(&s, swapfile);
    str_addz(&s,
        ".lock'\n"
        "        if command -v flock >/dev/null 2>&1 && ! flock -n 9; then\n"
        "            echo 'swap: another run is already building ");
    str_addz(&s, swapfile);
    str_addz(&s,
        " - skipping' >&2\n"
        "            exit 0\n"
        "        fi\n"
        "\n"
        "        swapoff '");
    str_addz(&s, swapfile);
    str_addz(&s,
        "' 2>/dev/null || true\n"
        "        rm -f '");
    str_addz(&s, swapfile);
    str_addz(&s,
        "'\n"
        "\n"
        "        # mkswap --file creates the file ITSELF, and that is the whole trick:\n"
        "        # it fallocates (instant, 0.04s for 4 GiB) and applies nocow on btrfs,\n"
        "        # both of which this module used to do by hand. Pre-creating the file\n"
        "        # with touch makes it fail - 'cannot set permissions on swap file:\n"
        "        # Success' - because it expects to own the creation. That failure used\n"
        "        # to be swallowed by 2>/dev/null and silently fell through to dd, which\n"
        "        # writes 4 GiB one megabyte at a time behind a spinner with no progress:\n"
        "        # indistinguishable from a hang, and the reason for the lock above.\n"
        "        #\n"
        "        # stderr is NOT redirected any more. If this fails the message is the\n"
        "        # only thing that explains the fallback, and it costs nothing to keep.\n"
        "        if mkswap -U clear --size ");
    str_addl(&s, p->file_want);
    str_addz(&s,
        "M --file '");
    str_addz(&s, swapfile);
    str_addz(&s,
        "'; then\n"
        "            :\n"
        "        else\n"
        "            # util-linux older than 2.38 has no --file. Allocate by hand, and\n"
        "            # prefer fallocate over dd for the same reason mkswap does.\n"
        "            rm -f '");
    str_addz(&s, swapfile);
    str_addz(&s,
        "'\n"
        "            touch '");
    str_addz(&s, swapfile);
    str_addz(&s,
        "'\n"
        "            chattr +C '");
    str_addz(&s, swapfile);
    str_addz(&s,
        "' 2>/dev/null || true\n"
        "            chmod 600 '");
    str_addz(&s, swapfile);
    str_addz(&s,
        "'\n"
        "            fallocate -l ");
    str_addl(&s, p->file_want);
    str_addz(&s,
        "M '");
    str_addz(&s, swapfile);
    str_addz(&s,
        "' 2>/dev/null ||\n"
        "                dd if=/dev/zero of='");
    str_addz(&s, swapfile);
    str_addz(&s,
        "' bs=1M count=");
    str_addl(&s, p->file_want);
    str_addz(&s,
        "\n"
        "            mkswap '");
    str_addz(&s, swapfile);
    str_addz(&s,
        "'\n"
        "        fi\n"
        "\n"
        "        chmod 600 '");
    str_addz(&s, swapfile);
    str_addz(&s,
        "'\n"
        "        swapon --priority ");
    str_addz(&s, str_text(&prio));
    str_addz(&s,
        " '");
    str_addz(&s, swapfile);
    str_addz(&s,
        "'\n"
        "        rm -f '");
    str_addz(&s, swapfile);
    str_addz(&s,
        ".lock'\n"
        "    ");
    argv[0] = (char *)"sh"; argv[1] = (char *)"-c"; argv[2] = s.p; argv[3] = NULL;
    rc = osr_run_root(argv);
    str_free(&s); str_free(&prio);
    return rc == 0;
}

/* add_fstab -- osr_ensure_line writes as OSR_USER; /etc/fstab needs root. */
static int add_fstab(void *ctx) {
    Str line;
    int ok;
    (void)ctx;
    str_init(&line);
    str_addz(&line, swapfile_path());
    str_addz(&line, " none swap defaults,pri=");
    str_addl(&line, file_prio());
    str_addz(&line, " 0 0\n");
    ok = osr_append_root(fstab_path(), str_text(&line));
    str_free(&line);
    return ok;
}

/* fstab_has_swapfile -- `grep -q "^[[:space:]]*$OSR_SWAPFILE[[:space:]]"`. */
static int fstab_has_swapfile(void) {
    char *buf;
    size_t len, pos = 0;
    Line line;
    size_t n = strlen(swapfile_path());
    int found = 0;

    buf = slurp(fstab_path(), &len);
    if (buf == NULL) return 0;
    while (!found && next_line(buf, len, &pos, &line)) {
        size_t i = 0;
        while (i < line.len && is_space(line.start[i])) i++;
        if (line.len - i > n && strncmp(line.start + i, swapfile_path(), n) == 0
            && is_space(line.start[i + n])) found = 1;
    }
    free(buf);
    return found;
}

int osrm_swap(void) {
    static const char *const zram_generator[] = { "zram-generator", NULL };
    static const char *const zramen[] = { "zramen", NULL };
    const char *virt = env_str("OSR_VIRT", "none");
    Plan p;
    Str msg, want;
    long disk_total;
    int ok = 1;

    /* A container/WSL guest does not own the memory it runs on - the host
     * does. */
    if (strcmp(virt, "docker") == 0 || strcmp(virt, "podman") == 0
        || strcmp(virt, "lxc") == 0 || strcmp(virt, "lxc-libvirt") == 0
        || strcmp(virt, "systemd-nspawn") == 0 || strcmp(virt, "wsl") == 0
        || strcmp(virt, "openvz") == 0) {
        osr_infof("swap: %s guest - the host owns swap/zram, skipping", virt);
        return 1;
    }

    swap_plan(&p);

    str_init(&msg);
    str_addz(&msg, "swap: ram="); str_addl(&msg, p.ram);
    str_addz(&msg, "M | zram want="); str_addl(&msg, p.zram_want);
    str_addz(&msg, "M have="); str_addl(&msg, p.have_zram);
    str_addz(&msg, "M | disk want="); str_addl(&msg, p.disk_want);
    str_addz(&msg, "M have partition="); str_addl(&msg, p.have_part);
    str_addz(&msg, "M file="); str_addl(&msg, p.have_file);
    str_addz(&msg, "M free="); str_addl(&msg, p.free);
    str_addz(&msg, "M -> swapfile "); str_addl(&msg, p.file_want);
    str_addc(&msg, 'M');
    osr_info(str_text(&msg));
    str_free(&msg);

    /* --- zram ------------------------------------------------------------- */
    str_init(&want);
    if (p.zram_want == 0) {
        osr_infof("swap: %ldM RAM is at/above the %ldM zram threshold - no zram "
                  "(keeps suspend-to-disk clean)",
                  p.ram, env_long("OSR_ZRAM_OFF_AT", 24576));
        if (p.have_zram > 0 || file_exists(zconf_path()))
            ok = osr_step("Disabling zram", disable_zram, NULL) && ok;
    } else if (strcmp(osr_mod_init(), "systemd") == 0) {
        ok = osr_pkg_install_step("Installing zram-generator", zram_generator) && ok;
        zram_conf(&want, &p);
        if (p.have_zram > 0 && same_text(zconf_path(), str_text(&want))) {
            osr_infof("zram already active at %ldM, skipping", p.zram_want);
        } else {
            Str desc;
            str_init(&desc);
            str_addz(&desc, "Configuring zram ("); str_addl(&desc, p.zram_want);
            str_addz(&desc, "M)");
            ok = osr_step(str_text(&desc), apply_zram, &p) && ok;
            str_free(&desc);
        }
    } else if (strcmp(osr_mod_init(), "runit") == 0) {
        ok = osr_pkg_install_step("Installing zramen", zramen) && ok;
        zramen_conf(&want, &p);
        if (p.have_zram > 0 && same_text(zramen_path(), str_text(&want))) {
            osr_infof("zram already active at %ldM, skipping", p.zram_want);
        } else {
            Str desc;
            str_init(&desc);
            str_addz(&desc, "Configuring zram via zramen ("); str_addl(&desc, p.zram_want);
            str_addz(&desc, "M, "); str_addl(&desc, p.zram_pct);
            str_addz(&desc, "% of RAM)");
            ok = osr_step(str_text(&desc), apply_zramen, &p) && ok;
            str_free(&desc);
        }
    } else {
        osr_warnf("swap: no zram setter for init=%s (systemd uses zram-generator, "
                  "runit uses zramen) - skipping zram",
                  *osr_mod_init() != '\0' ? osr_mod_init() : "unknown");
    }
    str_free(&want);

    /* --- disk swap --------------------------------------------------------- */
    if (p.file_want == 0) {
        osr_infof("swap: %ldM of swap partition covers the %ldM target, no swapfile needed",
                  p.have_part, p.disk_want);
    } else if (p.have_file == p.file_want) {
        osr_infof("swap: %s already %ldM, skipping", swapfile_path(), p.file_want);
    } else {
        Str desc;
        str_init(&desc);
        str_addz(&desc, "Creating "); str_addz(&desc, swapfile_path());
        str_addz(&desc, " ("); str_addl(&desc, p.file_want); str_addz(&desc, "M)");
        ok = osr_step(str_text(&desc), make_file, &p) && ok;
        str_free(&desc);
        if (fstab_has_swapfile()) {
            osr_infof("swap: %s already in %s", swapfile_path(), fstab_path());
        } else {
            str_init(&desc);
            str_addz(&desc, "Adding "); str_addz(&desc, swapfile_path());
            str_addz(&desc, " to "); str_addz(&desc, fstab_path());
            ok = osr_step(str_text(&desc), add_fstab, NULL) && ok;
            str_free(&desc);
        }
    }

    /* --- swappiness -------------------------------------------------------- */
    str_init(&want);
    sysctl_conf(&want, &p);
    if (same_text(sysctl_path(), str_text(&want))) {
        osr_infof("swap: vm.swappiness already %ld, skipping", p.swappiness);
    } else {
        Str desc;
        str_init(&desc);
        str_addz(&desc, "Setting vm.swappiness="); str_addl(&desc, p.swappiness);
        ok = osr_step(str_text(&desc), apply_sysctl, &p) && ok;
        str_free(&desc);
    }
    str_free(&want);

    /* Hibernation is a property of the DISK swap only - zram cannot hold the
     * image. */
    disk_total = (p.file_want > 0) ? p.have_part + p.file_want
                                   : p.have_part + p.have_file;
    if (disk_total >= p.ram)
        osr_infof("swap: %ldM of disk swap >= RAM - hibernation fits (add "
                  "resume=<swap dev> [+ resume_offset= for a swapfile] to the "
                  "kernel cmdline to actually use it)", disk_total);
    else
        osr_infof("swap: %ldM of disk swap < %ldM RAM - suspend-to-RAM only, no hibernation",
                  disk_total, p.ram);
    return ok;
}
