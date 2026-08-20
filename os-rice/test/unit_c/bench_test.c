/* test/unit_c/bench_test.c -- the parts of `osr benchmark cpu` that can be
 * checked without heating a CPU up.
 *
 * Which is most of the ones that can be wrong. Running stress-ng and reading a
 * real power sensor is I/O; the places a bug actually hides are the stress-ng
 * report parser, the wrapped-energy-counter arithmetic, and the two output
 * formats. All three are pure functions for exactly this reason.
 *
 * The wrap case deserves its own note: RAPL counters roll over every few
 * minutes under load, which is to say precisely when a benchmark is running,
 * and a naive subtraction turns that into a huge negative number at the worst
 * possible moment.
 */
/* Before any include: c_test.h pulls in <stdio.h>, which locks the
 * feature-test macros in. See uv_journal_test.c. */
#define _POSIX_C_SOURCE 200809L

#include "../c_test.h"

#include "../../lib/bench/cpu.c"
#include "../../lib/bench/power.c"
#include "../../lib/bench/util.c"
#include "../../lib/common.c"

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

/* --- stress-ng YAML ------------------------------------------------------- */

/* The real shape of a --yaml report, trimmed. The key detail is that
 * `bogo-ops-per-second-usr-sys-time` sits right next to the one we want and
 * shares a 19-character prefix with it. */
static const char *const YAML_ONE =
"---\n"
"system-info:\n"
"      stress-ng-version: 0.15.06\n"
"      run-by: root\n"
"metrics:\n"
"      - stressor: cpu\n"
"        bogo-ops: 24680\n"
"        bogo-ops-per-second-usr-sys-time: 154.321000\n"
"        bogo-ops-per-second-real-time: 1234.560000\n"
"        wall-clock-time: 20.000000\n"
"...\n";

static void test_yaml_single_stressor(void) {
    double ops = -1.0;
    osr_t_true("a stress-ng yaml report parses",
               bench_parse_yaml_ops(YAML_ONE, strlen(YAML_ONE), &ops));
    osr_t_eq_int("the real-time figure is the one taken", (long)ops, 1234L);
}

/* Several stressors sum, because an all-core run can report per-stressor rows
 * and the total throughput is what we are after. */
static void test_yaml_sums_stressors(void) {
    static const char *const yaml =
        "metrics:\n"
        "      - stressor: cpu\n"
        "        bogo-ops-per-second-real-time: 1000.000000\n"
        "      - stressor: cpu\n"
        "        bogo-ops-per-second-real-time: 500.500000\n";
    double ops = -1.0;
    osr_t_true("multi-stressor yaml parses", bench_parse_yaml_ops(yaml, strlen(yaml), &ops));
    osr_t_eq_int("throughput is summed", (long)(ops * 10), 15005L);
}

/* The neighbouring key must not be mistaken for ours: usr-sys time is a much
 * smaller number and silently using it would understate the machine. */
static void test_yaml_ignores_usr_sys_key(void) {
    static const char *const yaml =
        "        bogo-ops-per-second-usr-sys-time: 154.321000\n";
    double ops = -1.0;
    osr_t_true("a report with only the usr-sys key yields nothing",
               !bench_parse_yaml_ops(yaml, strlen(yaml), &ops));
}

static void test_yaml_rejects_junk(void) {
    double ops = -1.0;
    osr_t_true("empty input yields nothing", !bench_parse_yaml_ops("", 0, &ops));
    osr_t_true("unrelated text yields nothing",
               !bench_parse_yaml_ops("hello\nworld\n", 12, &ops));

    /* stress-ng killed mid-write leaves the key with no value. */
    {
        static const char *const torn = "        bogo-ops-per-second-real-time:";
        osr_t_true("a valueless key yields nothing",
                   !bench_parse_yaml_ops(torn, strlen(torn), &ops));
    }
    /* A value that is not a number must not contribute a salvaged prefix. */
    {
        static const char *const bad =
            "        bogo-ops-per-second-real-time: 12ab34\n";
        osr_t_true("a malformed value yields nothing",
                   !bench_parse_yaml_ops(bad, strlen(bad), &ops));
    }
}

/* A good row alongside a bad one keeps the good one and drops the bad one
 * entirely -- no partial credit from strtod. */
static void test_yaml_bad_row_does_not_pollute_total(void) {
    static const char *const yaml =
        "        bogo-ops-per-second-real-time: 1000.000000\n"
        "        bogo-ops-per-second-real-time: 99qq\n";
    double ops = -1.0;
    osr_t_true("parses", bench_parse_yaml_ops(yaml, strlen(yaml), &ops));
    osr_t_eq_int("only the valid row counted", (long)ops, 1000L);
}

/* --- energy counter arithmetic -------------------------------------------- */

static void test_energy_delta(void) {
    osr_t_eq_int("ordinary forward delta",
                 (long)pwr_energy_delta(1000UL, 5000UL, 100000UL), 4000L);
    osr_t_eq_int("zero delta",
                 (long)pwr_energy_delta(5000UL, 5000UL, 100000UL), 0L);

    /* Wrapped, with the range the kernel gave us: 100000 -> 10 means it went
     * 100000..99999 then 0..10. */
    osr_t_eq_int("wrap with a known range",
                 (long)pwr_energy_delta(99000UL, 10UL, 100000UL), 1010L);

    /* Wrapped with no declared range: assume the counter is 32-bit. Getting
     * this merely approximately right beats reporting negative energy. */
    osr_t_eq_int("wrap with no known range",
                 (long)pwr_energy_delta(0xFFFFFFF0UL, 15UL, 0UL), 31L);
}

/* --- the report ----------------------------------------------------------- */

static BenchResult sample_result(void) {
    BenchResult r;
    bench_result_init(&r);
    strcpy(r.cpu_model, "AMD Ryzen 7 9800X3D 8-Core Processor");
    r.ncpu = 16;
    r.have_single = 1; r.single_ops = 4812.0;
    r.have_all = 1;    r.all_ops = 31240.0;
    r.have_power = 1;  r.load_w = 88.4; r.idle_w = 21.3;
    strcpy(r.power_detail, "RAPL package energy counter");
    r.have_temp = 1;   r.peak_temp_c = 78.0;
    r.have_freq = 1;   r.peak_freq_khz = 5240000L;
    r.seconds = 62.0;
    return r;
}

static void test_report_has_the_numbers(void) {
    BenchResult r = sample_result();
    Str out;
    str_init(&out);
    bench_report(&r, &out);

    osr_t_true("reports single-core", strstr(str_text(&out), "single-core") != NULL);
    osr_t_true("reports the single-core figure", strstr(str_text(&out), "4812") != NULL);
    osr_t_true("reports all-core", strstr(str_text(&out), "31240") != NULL);
    osr_t_true("reports load and idle power together",
               strstr(str_text(&out), "88.4 W under load, 21.3 W idle") != NULL);
    /* 31240 / 88.4 = 353.4 -> 353. The number the whole exercise is about. */
    osr_t_true("reports efficiency in ops per watt",
               strstr(str_text(&out), "353 ops/s per watt") != NULL);
    osr_t_true("reports peak temperature", strstr(str_text(&out), "78 C") != NULL);
    osr_t_true("reports peak clock in MHz", strstr(str_text(&out), "5240 MHz") != NULL);

    str_free(&out);
}

/* With no sensor the report must say why rather than print a zero, which would
 * read as "this CPU uses no power". */
static void test_report_without_power_explains(void) {
    BenchResult r = sample_result();
    Str out;
    r.have_power = 0;
    r.load_w = 0.0;
    strcpy(r.power_detail, "no power sensor: no RAPL, no hwmon power input, no battery");
    str_init(&out);
    bench_report(&r, &out);

    osr_t_true("says why power is missing",
               strstr(str_text(&out), "no power sensor") != NULL);
    osr_t_true("and prints no watt figure", strstr(str_text(&out), " W under load") == NULL);
    osr_t_true("and no efficiency figure", strstr(str_text(&out), "per watt") == NULL);
    osr_t_true("but still reports throughput", strstr(str_text(&out), "31240") != NULL);
    str_free(&out);
}

/* --- json ----------------------------------------------------------------- */

static void test_json_shape(void) {
    BenchResult r = sample_result();
    Str out;
    str_init(&out);
    bench_json(&r, &out);

    osr_t_true("opens an object", str_text(&out)[0] == '{');
    osr_t_true("closes it", strstr(str_text(&out), "\n}") != NULL);
    osr_t_true("carries the cpu name",
               strstr(str_text(&out), "\"cpu\": \"AMD Ryzen 7 9800X3D") != NULL);
    osr_t_true("carries all_ops", strstr(str_text(&out), "\"all_ops\"") != NULL);
    osr_t_true("carries ops_per_watt", strstr(str_text(&out), "\"ops_per_watt\"") != NULL);
    osr_t_true("names the power source", strstr(str_text(&out), "\"power_source\"") != NULL);
    /* No trailing comma before the close, or it is not JSON at all. */
    osr_t_true("no dangling comma", strstr(str_text(&out), ",\n}") == NULL);

    str_free(&out);
}

/* A CPU model string containing a quote would otherwise break the file that
 * --save writes and --compare reads. */
static void test_json_escapes(void) {
    BenchResult r = sample_result();
    Str out;
    strcpy(r.cpu_model, "Weird \"quoted\" CPU\\model");
    str_init(&out);
    bench_json(&r, &out);
    osr_t_true("quotes and backslashes are escaped",
               strstr(str_text(&out), "\\\"quoted\\\" CPU\\\\model") != NULL);
    str_free(&out);
}

/* Omitted metrics must be absent rather than present-and-zero: the perf gate
 * divides by these. */
static void test_json_omits_missing(void) {
    BenchResult r;
    Str out;
    bench_result_init(&r);
    r.ncpu = 4;
    str_init(&out);
    bench_json(&r, &out);
    osr_t_true("no single_ops key when unmeasured",
               strstr(str_text(&out), "single_ops") == NULL);
    osr_t_true("no load_watts key when unmeasured",
               strstr(str_text(&out), "load_watts") == NULL);
    osr_t_true("still valid-looking", strstr(str_text(&out), ",\n}") == NULL);
    str_free(&out);
}

/* --- power source detection ----------------------------------------------- */

/* Whatever this machine has, detection must terminate and leave an explanation
 * behind -- a blank `detail` is the one outcome the report cannot render. */
static void test_pwr_detect_always_explains(void) {
    PwrMeter m;
    int found = pwr_detect(&m);
    osr_t_true("detection leaves a human explanation", m.detail[0] != '\0');
    if (!found) {
        osr_t_eq_int("no source means PWR_NONE", m.source, PWR_NONE);
    } else {
        osr_t_true("a found source names a file it can read", m.path[0] != '\0');
    }
    osr_t_eq_str("source names are stable", pwr_source_name(PWR_RAPL), "rapl");
    osr_t_eq_str("...for every source", pwr_source_name(PWR_NONE), "none");
}

/* An instantaneous meter with no samples must report failure, not 0 W. */
static void test_pwr_end_without_samples_fails(void) {
    PwrMeter m;
    double w = -1.0;
    memset(&m, 0, sizeof(m));
    m.source = PWR_HWMON;
    pwr_begin(&m);
    osr_t_true("no samples means no reading", !pwr_end(&m, &w));
    osr_t_eq_int("and the output is untouched", (long)w, -1L);
}

/* --- the CPU name --------------------------------------------------------- */

/* squeezed -- str_add_squeezed as a plain string function, for readability. */
static const char *squeezed(Str *tmp, const char *in) {
    str_reset(tmp);
    str_add_squeezed(tmp, in, strlen(in));
    return str_text(tmp);
}

/* Intel's brand string is a fixed-width field, so the padding is part of the
 * name that /proc/cpuinfo and lscpu both hand over. Every line that prints the
 * model -- the detect summary, the benchmark report, a saved baseline -- shows
 * the gap unless it is closed here. */
static void test_cpu_name_squeeze(void) {
    Str t;
    str_init(&t);

    osr_t_eq_str("interior padding collapses to one space",
                 squeezed(&t, "Intel(R) Core(TM) i7-8550U  CPU @ 1.80GHz"),
                 "Intel(R) Core(TM) i7-8550U CPU @ 1.80GHz");
    osr_t_eq_str("tabs count as whitespace too",
                 squeezed(&t, "Intel(R) Xeon(R)		W-2295 CPU"),
                 "Intel(R) Xeon(R) W-2295 CPU");
    osr_t_eq_str("both ends are trimmed",
                 squeezed(&t, "   AMD Ryzen 7 9800X3D 8-Core Processor 	 "),
                 "AMD Ryzen 7 9800X3D 8-Core Processor");
    osr_t_eq_str("a name with no runs is untouched",
                 squeezed(&t, "AMD Ryzen 7 5800X 8-Core Processor"),
                 "AMD Ryzen 7 5800X 8-Core Processor");
    osr_t_eq_str("all-whitespace squeezes to nothing", squeezed(&t, " \t  "), "");
    osr_t_eq_str("so does empty", squeezed(&t, ""), "");

    str_free(&t);
}

/* --- sensor detection, on machines this one is not -------------------------
 *
 * Every interesting case is a machine nobody running the suite is sitting at:
 * an Intel laptop whose powercap driver was never loaded, a kernel that made
 * the RAPL counters root-only, a part that publishes psys and no package
 * domain, a board whose hwmon chip offers `power1_average` and not
 * `power1_input`. Those are exactly the machines that report no power, so they
 * are built out of directories here and pointed at with OSR_POWERCAP /
 * OSR_HWMON / OSR_THERMAL.
 */

static char fx_root[512];

static void fx_mkdir(const char *rel) {
    char path[640];
    sprintf(path, "%s/%s", fx_root, rel);
    mkdir(path, 0755);
}

/* fx_write -- one sysfs-shaped attribute: a value and a newline. mode is the
 * point of several of these: 0400 is what a post-PLATYPUS energy_uj looks
 * like to a non-root reader. */
static void fx_write(const char *rel, const char *value, int mode) {
    char path[640];
    FILE *f;
    sprintf(path, "%s/%s", fx_root, rel);
    f = fopen(path, "w");
    if (f == NULL) return;
    fprintf(f, "%s\n", value);
    fclose(f);
    chmod(path, mode);
}

static void fx_reset(void) {
    char cmd[700];
    sprintf(cmd, "rm -rf %s && mkdir -p %s/powercap %s/hwmon %s/thermal %s/module %s/ps",
            fx_root, fx_root, fx_root, fx_root, fx_root, fx_root);
    if (system(cmd) != 0) return;
}

/* fx_rapl -- detect_rapl on its own.
 *
 * Not through pwr_detect: that walks on to hwmon and battery, either of which
 * may exist on the machine running the suite and would replace the RAPL
 * verdict these tests are about. */
static int fx_rapl(PwrMeter *m) {
    memset(m, 0, sizeof(*m));
    m->source = PWR_NONE;
    return detect_rapl(m);
}

static int fx_battery(PwrMeter *m) {
    memset(m, 0, sizeof(*m));
    m->source = PWR_NONE;
    return detect_battery(m);
}

static void fx_use(void) {
    char p[640];
    sprintf(p, "%s/powercap/", fx_root); setenv("OSR_POWERCAP", p, 1);
    sprintf(p, "%s/hwmon/", fx_root);    setenv("OSR_HWMON", p, 1);
    sprintf(p, "%s/thermal/", fx_root);  setenv("OSR_THERMAL", p, 1);
    /* Bare metal unless a case says otherwise. The suite itself may well be
     * running under WSL, which would otherwise change what the detector says
     * about a fixture that has nothing to do with WSL. */
    sprintf(p, "%s/osrelease", fx_root); setenv("OSR_OSRELEASE", p, 1);
    fx_write("osrelease", "6.12.0-generic", 0644);
    sprintf(p, "%s/module/", fx_root); setenv("OSR_SYSMODULE", p, 1);
    sprintf(p, "%s/ps/", fx_root);     setenv("OSR_POWER_SUPPLY", p, 1);
}

/* fx_be_wsl -- the same fixture, on a Hyper-V guest. */
static void fx_be_wsl(void) {
    fx_write("osrelease", "6.6.87.2-microsoft-standard-WSL2", 0644);
}

/* The one that matters for an Intel desktop reporting nothing: the tree is
 * there, and empty, because intel_rapl_msr is a module nobody loaded. The old
 * message for this was "no power sensor", which names no fix. */
static void test_empty_powercap_names_the_driver(void) {
    PwrMeter m;
    fx_reset();
    fx_use();
    osr_t_true("an empty powercap tree finds no source", !fx_rapl(&m));
    osr_t_true("...and blames the unloaded driver by name",
               strstr(m.detail, "intel_rapl_msr") != NULL);
}

/* The same empty tree in a Hyper-V guest, where loading that driver would
 * achieve nothing. Same symptom, opposite advice -- which is the whole reason
 * the check exists. */
static void test_empty_powercap_under_wsl_does_not(void) {
    PwrMeter m;
    fx_reset();
    fx_use();
    fx_be_wsl();
    osr_t_true("an empty powercap tree in WSL finds no source", !fx_rapl(&m));
    osr_t_true("...and does not send the reader to modprobe",
               strstr(m.detail, "intel_rapl_msr") == NULL);
    osr_t_true("...naming Hyper-V as the reason",
               strstr(m.detail, "Hyper-V") != NULL);
}

/* A kernel without CONFIG_POWERCAP at all is a different diagnosis: there is
 * no module to load, and telling someone to modprobe one wastes their time. */
static void test_absent_powercap_is_distinct(void) {
    PwrMeter m;
    fx_reset();
    fx_use();
    setenv("OSR_POWERCAP", "/nonexistent-powercap/", 1);
    osr_t_true("an absent powercap tree finds no source", !fx_rapl(&m));
    osr_t_true("...and says the kernel lacks it",
               strstr(m.detail, "no powercap support") != NULL);
    osr_t_true("...without suggesting a modprobe",
               strstr(m.detail, "intel_rapl_msr") == NULL);
}

/* The M390 case: a 2010 Westmere laptop. Intel introduced RAPL with Sandy
 * Bridge, so the driver loads, probes, registers nothing, and leaves an empty
 * tree that looks exactly like a driver that was never loaded. Telling someone
 * to load a module they have already loaded is the worst possible answer. */
static void test_loaded_driver_and_empty_tree_means_no_rapl(void) {
    PwrMeter m;
    fx_reset();
    fx_use();
    fx_mkdir("module/intel_rapl_msr");

    osr_t_true("an empty tree with the driver loaded finds no source", !fx_rapl(&m));
    osr_t_true("...and does not repeat the modprobe advice",
               strstr(m.detail, "not loaded") == NULL);
    osr_t_true("...saying the CPU has no RAPL instead",
               strstr(m.detail, "no RAPL on this CPU") != NULL);
    osr_t_true("...and naming the generation that gained it",
               strstr(m.detail, "Sandy Bridge") != NULL);
}

/* A laptop with no RAPL and no hwmon rail can still measure power -- on
 * battery. That makes "you are on AC" an instruction, not a dead end. */
static void test_battery_on_ac_says_to_unplug(void) {
    PwrMeter m;
    fx_reset();
    fx_use();
    fx_mkdir("ps/BAT0");
    fx_write("ps/BAT0/status", "Full", 0644);
    fx_write("ps/BAT0/voltage_now", "11100000", 0644);

    osr_t_true("a charging battery is not a power source", !fx_battery(&m));
    osr_t_true("...and the fix is to unplug it",
               strstr(m.detail, "unplug AC") != NULL);
}

/* Batteries from before roughly 2012 -- which is every machine that also has
 * no RAPL -- publish current and voltage, not power. Reading only power_now
 * meant the one machine that needs this fallback never got a number. */
static void test_battery_current_times_voltage(void) {
    PwrMeter m;
    double w = 0.0;
    fx_reset();
    fx_use();
    fx_mkdir("ps/BAT0");
    fx_write("ps/BAT0/status", "Discharging", 0644);
    fx_write("ps/BAT0/current_now", "1850000", 0644);   /* 1.85 A */
    fx_write("ps/BAT0/voltage_now", "11100000", 0644);  /* 11.1 V */

    osr_t_true("a discharging battery with no power_now is still a source",
               fx_battery(&m));
    osr_t_eq_int("...as battery", (long)m.source, (long)PWR_BATTERY);
    osr_t_true("...flagged as whole-system", strstr(m.detail, "whole system") != NULL);

    pwr_begin(&m);
    pwr_sample(&m);
    osr_t_true("...and it reads back", pwr_end(&m, &w));
    /* 1.85 A x 11.1 V = 20.535 W. The product of two microunits is picowatts,
     * which overflows a 32-bit long by four orders of magnitude -- the reason
     * the arithmetic is in double. */
    osr_t_eq_int("...at current x voltage", (long)(w * 100.0 + 0.5), 2054L);
}

/* Where both shapes exist, power_now is the battery's own figure and needs no
 * arithmetic, so it wins. */
static void test_battery_prefers_power_now(void) {
    PwrMeter m;
    double w = 0.0;
    fx_reset();
    fx_use();
    fx_mkdir("ps/BAT0");
    fx_write("ps/BAT0/status", "Discharging", 0644);
    fx_write("ps/BAT0/power_now", "15000000", 0644);    /* 15 W */
    fx_write("ps/BAT0/current_now", "1850000", 0644);
    fx_write("ps/BAT0/voltage_now", "11100000", 0644);

    osr_t_true("power_now is a source", fx_battery(&m));
    osr_t_true("...and is the one read", strstr(m.path, "power_now") != NULL);
    pwr_begin(&m);
    pwr_sample(&m);
    osr_t_true("...reading back", pwr_end(&m, &w));
    osr_t_eq_int("...as watts, not a product", (long)(w + 0.5), 15L);
}

static void test_package_beats_its_children(void) {
    PwrMeter m;
    fx_reset();
    fx_use();
    fx_mkdir("powercap/intel-rapl:0");
    fx_write("powercap/intel-rapl:0/name", "package-0", 0644);
    fx_write("powercap/intel-rapl:0/energy_uj", "123456789", 0644);
    fx_write("powercap/intel-rapl:0/max_energy_range_uj", "262143328850", 0644);
    /* the core domain, which is a fraction of the package */
    fx_mkdir("powercap/intel-rapl:0:0");
    fx_write("powercap/intel-rapl:0:0/name", "core", 0644);
    fx_write("powercap/intel-rapl:0:0/energy_uj", "60000000", 0644);

    osr_t_true("a package domain is found", pwr_detect(&m));
    osr_t_eq_int("...as RAPL", (long)m.source, (long)PWR_RAPL);
    osr_t_true("...reading the package, not the core child",
               strstr(m.path, "intel-rapl:0/energy_uj") != NULL);
    osr_t_eq_int("...with the wrap point read", (long)(m.max_range_uj / 1000000UL), 262143L);
}

/* Intel laptops since Skylake often publish psys and no package domain. Wider
 * than the CPU, but a real reading -- and the report has to say so. */
static void test_psys_is_used_and_labelled(void) {
    PwrMeter m;
    fx_reset();
    fx_use();
    fx_mkdir("powercap/intel-rapl-mmio:0");
    fx_write("powercap/intel-rapl-mmio:0/name", "psys", 0644);
    fx_write("powercap/intel-rapl-mmio:0/energy_uj", "555000", 0644);

    osr_t_true("psys alone is still a power source", pwr_detect(&m));
    osr_t_eq_int("...as RAPL", (long)m.source, (long)PWR_RAPL);
    osr_t_true("...labelled as the platform domain, not the package",
               strstr(m.detail, "psys") != NULL);
    osr_t_true("...and warned about", strstr(m.detail, "whole board") != NULL);
}

static void test_package_wins_over_psys(void) {
    PwrMeter m;
    fx_reset();
    fx_use();
    fx_mkdir("powercap/intel-rapl:1");
    fx_write("powercap/intel-rapl:1/name", "psys", 0644);
    fx_write("powercap/intel-rapl:1/energy_uj", "555000", 0644);
    fx_mkdir("powercap/intel-rapl:0");
    fx_write("powercap/intel-rapl:0/name", "package-0", 0644);
    fx_write("powercap/intel-rapl:0/energy_uj", "123456789", 0644);

    osr_t_true("both present, one is chosen", pwr_detect(&m));
    osr_t_true("...and it is the package", strstr(m.path, "intel-rapl:0/") != NULL);
}

/* 0400 since CVE-2020-8694. The fix is `sudo`, and the message has to say it.
 * Skipped when the suite runs as root, where the mode is not a barrier. */
static void test_rootonly_rapl_says_so(void) {
    PwrMeter m;
    if (geteuid() == 0) {
        osr_t_ok("root-only RAPL: skipped, this test runs as root");
        return;
    }
    fx_reset();
    fx_use();
    fx_mkdir("powercap/intel-rapl:0");
    fx_write("powercap/intel-rapl:0/name", "package-0", 0644);
    /* Mode 0, not 0400: the suite runs as the file's OWNER, and 0400 is
     * readable by its owner. 0 is the only mode that denies a non-root reader
     * the way a root-owned 0400 denies this user on a real machine. */
    fx_write("powercap/intel-rapl:0/energy_uj", "123456789", 0);

    osr_t_true("an unreadable counter is not a source", !fx_rapl(&m));
    osr_t_true("...and the fix is named", strstr(m.detail, "root") != NULL);
}

/* Several super-I/O and BMC chips publish only the averaging window. */
static void test_hwmon_average_is_accepted(void) {
    PwrMeter m;
    fx_reset();
    fx_use();
    setenv("OSR_POWERCAP", "/nonexistent-powercap/", 1);
    fx_mkdir("hwmon/hwmon3");
    fx_write("hwmon/hwmon3/name", "nct6687", 0644);
    fx_write("hwmon/hwmon3/power1_average", "42000000", 0644);

    osr_t_true("power1_average counts as a sensor", pwr_detect(&m));
    osr_t_eq_int("...as hwmon", (long)m.source, (long)PWR_HWMON);
    osr_t_true("...and the driver is named", strstr(m.detail, "nct6687") != NULL);
}

/* On a big Intel part temp1..tempN are the cores and the package is somewhere
 * further along. Picking temp1 reports one core, which runs cooler than the
 * package under a single-threaded load -- the exact phase this benchmark has. */
static void test_temp_prefers_the_labelled_package(void) {
    char out[BENCH_PATH_MAX];
    fx_reset();
    fx_use();
    fx_mkdir("hwmon/hwmon2");
    fx_write("hwmon/hwmon2/name", "coretemp", 0644);
    fx_write("hwmon/hwmon2/temp1_label", "Core 0", 0644);
    fx_write("hwmon/hwmon2/temp1_input", "51000", 0644);
    fx_write("hwmon/hwmon2/temp5_label", "Package id 0", 0644);
    fx_write("hwmon/hwmon2/temp5_input", "67000", 0644);

    out[0] = '\0';
    osr_t_true("a labelled hwmon temperature is found", find_cpu_temp(out, sizeof(out)));
    osr_t_true("...and it is the package, not core 0",
               strstr(out, "temp5_input") != NULL);
}

/* The last resort: no hwmon at all, one thermal zone. x86_pkg_temp must win
 * over acpitz even though acpitz is the lower-numbered zone. */
static void test_temp_falls_back_to_thermal_zones(void) {
    char out[BENCH_PATH_MAX];
    fx_reset();
    fx_use();
    fx_mkdir("thermal/thermal_zone0");
    fx_write("thermal/thermal_zone0/type", "acpitz", 0644);
    fx_write("thermal/thermal_zone0/temp", "27800", 0644);
    fx_mkdir("thermal/thermal_zone3");
    fx_write("thermal/thermal_zone3/type", "x86_pkg_temp", 0644);
    fx_write("thermal/thermal_zone3/temp", "64000", 0644);

    out[0] = '\0';
    osr_t_true("a thermal zone is found when hwmon has nothing",
               find_cpu_temp(out, sizeof(out)));
    osr_t_true("...and the package zone beats the lower-numbered acpitz",
               strstr(out, "thermal_zone3") != NULL);
}

static void run_detection_tests(void) {
    const char *tmp = getenv("TMPDIR");
    sprintf(fx_root, "%s/osr-bench-fx-%ld", tmp != NULL ? tmp : "/tmp", (long)getpid());

    test_empty_powercap_names_the_driver();
    test_empty_powercap_under_wsl_does_not();
    test_loaded_driver_and_empty_tree_means_no_rapl();
    test_battery_on_ac_says_to_unplug();
    test_battery_current_times_voltage();
    test_battery_prefers_power_now();
    test_absent_powercap_is_distinct();
    test_package_beats_its_children();
    test_psys_is_used_and_labelled();
    test_package_wins_over_psys();
    test_rootonly_rapl_says_so();
    test_hwmon_average_is_accepted();
    test_temp_prefers_the_labelled_package();
    test_temp_falls_back_to_thermal_zones();

    {
        char cmd[700];
        sprintf(cmd, "rm -rf %s", fx_root);
        if (system(cmd) != 0) return;
    }
    unsetenv("OSR_POWERCAP");
    unsetenv("OSR_HWMON");
    unsetenv("OSR_THERMAL");
    unsetenv("OSR_OSRELEASE");
    unsetenv("OSR_SYSMODULE");
    unsetenv("OSR_POWER_SUPPLY");
}

int main(void) {
    OSR_T_INIT();

    run_detection_tests();


    test_cpu_name_squeeze();

    test_yaml_single_stressor();
    test_yaml_sums_stressors();
    test_yaml_ignores_usr_sys_key();
    test_yaml_rejects_junk();
    test_yaml_bad_row_does_not_pollute_total();

    test_energy_delta();

    test_report_has_the_numbers();
    test_report_without_power_explains();

    test_json_shape();
    test_json_escapes();
    test_json_omits_missing();

    test_pwr_detect_always_explains();
    test_pwr_end_without_samples_fails();

    return osr_t_finish();
}
