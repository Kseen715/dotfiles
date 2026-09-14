/* test/unit_c/fans_test.c -- which fan headers modules/fans.c takes over, and
 * which temperature it is willing to steer them by.
 *
 * The module's whole value is in two refusals, so those are what this asserts
 * hardest:
 *
 *   1. It touches ONLY channels reading pwm<n>_enable = 0 -- pinned at full
 *      speed with nobody steering them. A channel on manual, or on one of the
 *      chip's own Smart Fan modes, was set by someone else and stays theirs.
 *
 *   2. It steers by a CPU die sensor or not at all. The board this was written
 *      against (NCT6779D on a HUANANZHI X79) reports SYSTIN at 120C and
 *      AUXTIN0 at 17.5C on a cold box, and its unmanaged headers are wired to
 *      AUXTIN0 -- so a curve over the chip's own sensors is a curve over
 *      nothing. With no coretemp/k10temp present the module must decline and
 *      leave the fans loud, which is the safe direction.
 *
 * Hermetic: the hwmon tree, /etc/fancontrol and the modules-load drop-in are
 * all fixture paths the module lets the caller name, and sudo executes only
 * the harmless writes -- no modprobe, systemctl or package manager ever
 * reaches the machine running the suite.
 *
 * See test/harness.h.
 */
#include "../harness.c"

static OsrSandbox sb;

#define HWMON "sys/class/hwmon"

static void said(const char *needle, const char *label) {
    osr_assert_true(strstr(osr_sb_capture_both(&sb), needle) != NULL, label);
}
static void quiet_about(const char *needle, const char *label) {
    osr_assert_true(strstr(osr_sb_capture_both(&sb), needle) == NULL, label);
}

static char *conf(void) {
    HStr path;
    char *got;
    hs_init(&path);
    hs_path(&path, hs_text(&sb.root), "fancontrol");
    got = h_slurp(hs_text(&path));
    hs_free(&path);
    return got;
}
static void conf_holds(const char *needle, const char *label) {
    char *got = conf();
    osr_assert_true(strstr(got, needle) != NULL, label);
    free(got);
}
static void conf_lacks(const char *needle, const char *label) {
    char *got = conf();
    osr_assert_true(strstr(got, needle) == NULL, label);
    free(got);
}

/* attr -- one hwmon sysfs file, e.g. hwmon2/pwm3_enable. */
static void attr(const char *hwmon, const char *leaf, const char *value) {
    HStr rel;
    hs_init(&rel);
    hs_add(&rel, HWMON "/");
    hs_add(&rel, hwmon);
    hs_add(&rel, "/");
    hs_add(&rel, leaf);
    osr_sb_write(&sb, hs_text(&rel), value, 0644);
    hs_free(&rel);
}

/* superio -- an NCT6779D at hwmon2 with five headers:
 *   pwm1 manual (someone set it), pwm2 on the chip's Smart Fan IV,
 *   pwm3/4/5 pinned at 255 with nobody steering them. Only pwm4 has a
 *   tachometer that actually reports. */
static void superio(void) {
    osr_sb_mkdir(&sb, HWMON "/hwmon2");
    attr("hwmon2", "name", "nct6779\n");
    attr("hwmon2", "pwm1", "127\n");        attr("hwmon2", "pwm1_enable", "1\n");
    attr("hwmon2", "pwm2", "40\n");         attr("hwmon2", "pwm2_enable", "5\n");
    attr("hwmon2", "pwm3", "255\n");        attr("hwmon2", "pwm3_enable", "0\n");
    attr("hwmon2", "pwm4", "255\n");        attr("hwmon2", "pwm4_enable", "0\n");
    attr("hwmon2", "pwm5", "255\n");        attr("hwmon2", "pwm5_enable", "0\n");
    attr("hwmon2", "fan2_input", "698\n");
    attr("hwmon2", "fan3_input", "0\n");
    attr("hwmon2", "fan4_input", "1100\n");
    attr("hwmon2", "fan5_input", "0\n");
    /* The board's own temperatures, exactly as that board reports them when it
     * is cold: nonsense, and never what the curve may read. */
    attr("hwmon2", "temp1_label", "SYSTIN\n");   attr("hwmon2", "temp1_input", "120000\n");
    attr("hwmon2", "temp2_label", "CPUTIN\n");   attr("hwmon2", "temp2_input", "127500\n");
    attr("hwmon2", "temp3_label", "AUXTIN0\n");  attr("hwmon2", "temp3_input", "17500\n");

    osr_sb_mkdir(&sb, "sys/devices/platform/nct6775.2576");
    osr_sb_mkdir(&sb, "sys/bus/platform/drivers/nct6775");
    osr_sb_symlink(&sb, "sys/bus/platform/drivers/nct6775",
                   "sys/devices/platform/nct6775.2576/driver");
    osr_sb_symlink(&sb, "sys/devices/platform/nct6775.2576",
                   HWMON "/hwmon2/device");
}

/* coretemp -- the die sensor at hwmon0, package plus one core. */
static void coretemp(void) {
    osr_sb_mkdir(&sb, HWMON "/hwmon0");
    attr("hwmon0", "name", "coretemp\n");
    attr("hwmon0", "temp1_label", "Package id 0\n");
    attr("hwmon0", "temp1_input", "48000\n");
    attr("hwmon0", "temp2_label", "Core 0\n");
    attr("hwmon0", "temp2_input", "45000\n");
    osr_sb_mkdir(&sb, "sys/devices/platform/coretemp.0");
    osr_sb_symlink(&sb, "sys/devices/platform/coretemp.0", HWMON "/hwmon0/device");
}

static void clean(void) {
    osr_sb_rm(&sb, "sys");
    osr_sb_rm(&sb, "fancontrol");
    osr_sb_rm(&sb, "modules-load.d");
    osr_sb_mkdir(&sb, HWMON);
    osr_sb_reset(&sb);
}

static void run(const char *virt) {
    osr_sb_env(&sb, "OSR_VIRT", virt != NULL ? virt : "none");
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "module", "run", "fans", (const char *)NULL);
}

int main(void) {
    HStr p;

    osr_sb_init(&sb);
    hs_init(&p);

    osr_sb_env_arch(&sb);
    osr_sb_env(&sb, "OSR_INIT", "systemd");

    hs_path(&p, hs_text(&sb.root), HWMON);
    osr_sb_env(&sb, "OSR_HWMON_DIR", hs_text(&p));
    hs_path(&p, hs_text(&sb.root), "fancontrol");
    osr_sb_env(&sb, "OSR_FANCONTROL_CONF", hs_text(&p));
    hs_path(&p, hs_text(&sb.root), "modules-load.d/99-osr-fans.conf");
    osr_sb_env(&sb, "OSR_MODULES_LOAD_CONF", hs_text(&p));

    osr_sb_stub_body(&sb, "sudo",
        "printf 'sudo %s\\n' \"$*\" >>\"$LOG\"\n"
        "case \"$1\" in tee|mkdir|rm|cp) exec \"$@\" ;; esac\n"
        "exit 0\n");
    osr_sb_stub(&sb, "modprobe", 0);
    osr_sb_stub(&sb, "systemctl", 0);
    /* Nothing is installed, so every install is attempted and shows up in the
     * escalation log; pacman itself never runs. */
    osr_sb_stub_body(&sb, "pacman", "[ \"$1\" = \"-Q\" ] && exit 1\nexit 0\n");

    /* ================================================================
     * 1. The board this was written for
     * ================================================================ */
    clean();
    superio();
    coretemp();
    run(NULL);

    said("pwm3,4,5", "only the headers pinned at full speed are taken over");
    quiet_about("pwm1,", "the header someone set by hand is left alone");
    conf_holds("hwmon2/pwm3=hwmon0/temp1_input",
        "the curve is steered by the CPU package sensor");
    conf_holds("hwmon2/pwm5=hwmon0/temp1_input",
        "every taken-over header reads that same sensor");
    conf_lacks("pwm2=",
        "a header already on the chip's own Smart Fan curve is never rewritten");
    conf_lacks("hwmon2/temp",
        "no super-I/O board temperature ends up steering anything -- on this "
        "board they read 120C cold");
    conf_holds("FCFANS=hwmon2/pwm4=hwmon2/fan4_input",
        "the one header with a live tachometer gets spin-up verification");
    conf_lacks("fan3_input",
        "a header whose tachometer reads 0 is configured without one, rather "
        "than declared dead and run at full speed");
    conf_holds("MINPWM=hwmon2/pwm3=38",
        "idle duty is the quiet floor, not the BIOS's 255");
    conf_holds("MAXPWM=hwmon2/pwm3=255",
        "and the ramp still reaches full speed");
    conf_holds("MINTEMP=hwmon2/pwm3=45", "the floor holds to 45C");
    conf_holds("MAXTEMP=hwmon2/pwm3=75", "and full speed arrives at 75C");
    conf_holds("DEVNAME=hwmon2=nct6779 hwmon0=coretemp",
        "both hwmons are named, so fancontrol re-finds them after a renumber");
    conf_holds("DEVPATH=hwmon2=devices/platform/nct6775.2576",
        "and addressed by their stable device paths");
    {
        char *got;
        hs_path(&p, hs_text(&sb.root), "modules-load.d/99-osr-fans.conf");
        got = h_slurp(hs_text(&p));
        osr_assert_true(strstr(got, "nct6775") != NULL,
            "the super-I/O driver is loaded at boot -- nothing auto-binds it, "
            "and a curve over an hwmon that does not exist yet is a service "
            "that fails");
        free(got);
    }

    /* ================================================================
     * 2. A rerun changes nothing
     * ================================================================ */
    osr_sb_reset(&sb);
    run(NULL);
    said("already the one this module writes",
        "a rerun recognises its own config and rewrites nothing");

    /* ================================================================
     * 3. No CPU die sensor: refuse rather than guess
     * ================================================================ */
    clean();
    superio();
    run(NULL);
    said("refusing to build a curve on super-I/O board temperatures",
        "with no trustworthy sensor the module declines");
    quiet_about("Installing fancontrol",
        "and installs nothing it would have no use for");
    {
        char *got = conf();
        osr_assert_true(got[0] == '\0',
            "no config is written, so the fans stay loud -- the safe direction");
        free(got);
    }

    /* ================================================================
     * 4. Every header already managed
     * ================================================================ */
    clean();
    superio();
    coretemp();
    attr("hwmon2", "pwm3_enable", "5\n");
    attr("hwmon2", "pwm4_enable", "5\n");
    attr("hwmon2", "pwm5_enable", "5\n");
    osr_sb_reset(&sb);
    run(NULL);
    said("nothing to quieten",
        "a board whose headers are all steered already is left entirely alone");
    quiet_about("Installing fancontrol",
        "and no daemon is installed to fight the chip for them");

    /* ================================================================
     * 5. A guest has no fans
     * ================================================================ */
    clean();
    superio();
    coretemp();
    run("kvm");
    said("guest - fans belong to the host",
        "in a VM the module does nothing: the hwmon it can see is not the "
        "hardware it would be steering");
    quiet_about("Installing fancontrol", "and installs nothing there either");

    hs_free(&p);
    osr_sb_free(&sb);
    return osr_finish();
}
