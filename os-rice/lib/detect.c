/* lib/detect.c -- detect the host once.
 *
 *   all          every fact, as shell assignments to eval
 *   ram          just the RAM facets (the runner re-probes them after sudo)
 *   cpu|gpu|npu|virt   the individual probes, same shape
 *   gpu-chip <vendor>  the chip codename of the first <vendor> GPU
 *
 * Sets OSR_DISTRO/OSR_PKG/OSR_INIT plus the release, arch and config-path
 * facets the map @qualifier resolver (section 1) and the preconditions
 * (section 10) read, then the hardware facets (section 7): CPU id, RAM,
 * GPU/NPU vendor, virtualization.
 *
 * ONE FILE, TWO BODIES, and here they share nothing but the NAMES -- which is
 * the whole of what has to be shared. Those names are what lib/pkgmap's
 * `name@facet` keys are matched against and what a rice's `require:` line
 * reads, so a facet that means one thing on one system and another elsewhere
 * would silently resolve the wrong package. Everything under them differs: a
 * Linux box answers out of /etc/os-release, /proc and /sys, a Windows one out
 * of the registry and a few system calls.
 *
 * The POSIX probes are the sh ones, command for command: lscpu, lspci -mm,
 * dmidecode -t 17 (with the unprivileged sudo -n retry), /proc/meminfo,
 * /sys/class/drm, /sys/class/accel, systemd-detect-virt. Same order, same
 * fallbacks, same "silent and command-guarded so a minimal box never errors"
 * rule -- and the same override knobs (OSR_MEMINFO, OSR_DRM, OSR_ACCEL), which
 * is what makes any of it testable.
 *
 * What every probe must answer, and what each fallback is for, is stated in
 * test/unit_c/detect_test.c.
 *
 * C89 + POSIX, and C89 + Win32.
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "common.h"
#include "cmds.h"
#include "module.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <ctype.h>
#include <glob.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

typedef struct {
    Str distro, id_like, codename, version_id, version;
    Str arch, arch_deb, pkg, init, etc_default;
    Str cpu_vendor, cpu_model, cpu_arch;
    long cpu_cores, cpu_threads;
    Str ram_total, ram_type, ram_speed;
    long ram_sticks, ram_channels;
    Str gpu_vendor, gpu_model, gpu_devices;
    long gpu_count;
    Str npu_vendor;
    long npu_count;
    Str virt;
} Facts;

static void facts_init(Facts *f) {
    str_init(&f->distro); str_init(&f->id_like); str_init(&f->codename);
    str_init(&f->version_id); str_init(&f->version);
    str_init(&f->arch); str_init(&f->arch_deb); str_init(&f->pkg);
    str_init(&f->init); str_init(&f->etc_default);
    str_init(&f->cpu_vendor); str_init(&f->cpu_model); str_init(&f->cpu_arch);
    f->cpu_cores = 0; f->cpu_threads = 0;
    str_init(&f->ram_total); str_init(&f->ram_type); str_init(&f->ram_speed);
    f->ram_sticks = 0; f->ram_channels = 0;
    str_init(&f->gpu_vendor); str_init(&f->gpu_model); str_init(&f->gpu_devices);
    f->gpu_count = 0;
    str_init(&f->npu_vendor); f->npu_count = 0;
    str_init(&f->virt);
}

/* Sink -- where a detected fact goes: into a shell assignment for a
 * caller to eval, or straight into this process's environment. Declared
 * here, above the split, because both bodies publish through it and the
 * set of names they publish is the contract between them. */
typedef void (*Sink)(void *ctx, const char *name, const char *value);

#ifndef _WIN32

/* --- little helpers -------------------------------------------------------- */

/* have_cmd -- `command -v <name>`: an executable of that name on $PATH. The
 * resolved path goes into out when it is wanted (dmidecode needs it: sudo's
 * secure_path would otherwise pick a different one than PATH selected). */
static int have_cmd(const char *name, Str *out) {
    const char *path = env_str("PATH", "");
    const char *p = path;
    Str candidate;
    int found = 0;

    if (strchr(name, '/') != NULL) return access(name, X_OK) == 0;
    str_init(&candidate);
    while (!found) {
        const char *colon = strchr(p, ':');
        size_t len = (colon != NULL) ? (size_t)(colon - p) : strlen(p);
        str_reset(&candidate);
        if (len == 0) str_addc(&candidate, '.');
        else str_add(&candidate, p, len);
        str_addc(&candidate, '/');
        str_addz(&candidate, name);
        if (access(str_text(&candidate), X_OK) == 0) {
            found = 1;
            if (out != NULL) str_addz(out, str_text(&candidate));
        }
        if (colon == NULL) break;
        p = colon + 1;
    }
    str_free(&candidate);
    return found;
}

/* run_capture -- a command's stdout, stderr discarded, as the sh probes ran
 * them (`lscpu 2>/dev/null`). NULL when it cannot be started. */
static char *run_capture(const char *cmd) {
    Str line;
    FILE *fp;
    Str out;
    int c;

    str_init(&line);
    str_addz(&line, cmd);
    str_addz(&line, " 2>/dev/null");
    fp = popen(str_text(&line), "r");
    str_free(&line);
    if (fp == NULL) return NULL;
    str_init(&out);
    while ((c = fgetc(fp)) != EOF) str_addc(&out, (char)c);
    pclose(fp);
    if (out.p == NULL) str_addc(&out, '\0'), out.len = 0;
    return out.p;
}

/* trim_field -- the `gsub(/^[ \t]+/,"",$2)` every lscpu awk did. */
static void trim_field(Str *out, const char *s) {
    const char *end;
    while (*s == ' ' || *s == '\t') s++;
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) end--;
    str_add(out, s, (size_t)(end - s));
}

/* field_after -- the value of the first "<label>" line of a `key: value`
 * listing (lscpu's output), trimmed. */
static int field_after(Str *out, const char *text, const char *label) {
    size_t len = text != NULL ? strlen(text) : 0;
    size_t pos = 0;
    Line line;
    size_t llen = strlen(label);

    while (next_line(text != NULL ? text : "", len, &pos, &line)) {
        if (line.len > llen && strncmp(line.start, label, llen) == 0) {
            Str value;
            str_init(&value);
            str_add(&value, line.start + llen, line.len - llen);
            trim_field(out, str_text(&value));
            str_free(&value);
            return 1;
        }
    }
    return 0;
}

/* uniq_add -- _osr_uniq_add: append to a space-separated list iff absent. */
static void uniq_add(Str *list, const char *item) {
    const char *p = str_text(list);
    size_t ilen = strlen(item);
    while (*p != '\0') {
        const char *start = p;
        while (*p != '\0' && *p != ' ') p++;
        if ((size_t)(p - start) == ilen && strncmp(start, item, ilen) == 0) return;
        while (*p == ' ') p++;
    }
    if (list->len > 0) str_addc(list, ' ');
    str_addz(list, item);
}

static int contains(const char *hay, const char *needle) {
    return hay != NULL && strstr(hay, needle) != NULL;
}

/* norm_gpu -- _osr_norm_gpu: a vendor/model string to a canonical tag. */
static const char *norm_gpu(const char *text) {
    if (contains(text, "NVIDIA") || contains(text, "nVidia") || contains(text, "GeForce") ||
        contains(text, "Quadro") || contains(text, "Tesla") || contains(text, "RTX") ||
        contains(text, "GTX")) return "NVIDIA";
    if (contains(text, "AMD") || contains(text, "ATI") || contains(text, "Radeon")) return "AMD";
    if (contains(text, "Intel") || contains(text, "HD Graphics") || contains(text, "Iris") ||
        contains(text, "Arc")) return "Intel";
    if (contains(text, "VMware") || contains(text, "VMWARE")) return "VMware";
    if (contains(text, "VirtualBox") || contains(text, "VBOX")) return "VirtualBox";
    if (contains(text, "QEMU") || contains(text, "virtio") || contains(text, "Red Hat") ||
        contains(text, "QXL")) return "QEMU";
    if (contains(text, "Microsoft") || contains(text, "Hyper-V")) return "Microsoft";
    if (contains(text, "Cirrus")) return "Cirrus";
    /* Device-tree compatible strings, for SoCs with no PCI GPU at all: the
     * Pi's "brcm,bcm2711-vc5", a Mali "arm,mali-g52", "qcom,adreno". */
    if (contains(text, "brcm") || contains(text, "Broadcom") ||
        contains(text, "vc4") || contains(text, "vc5") || contains(text, "v3d")) return "Broadcom";
    if (contains(text, "mali") || contains(text, "arm,")) return "ARM";
    if (contains(text, "qcom") || contains(text, "adreno")) return "Qualcomm";
    if (contains(text, "img,") || contains(text, "powervr")) return "Imagination";
    return "Unknown";
}

/* quoted_field -- `cut -d'"' -f<n>` over an lspci -mm line: fields are
 * numbered from 1 with the delimiter splitting, so the quoted strings land on
 * the even ones (4 = vendor, 6 = device). */
static void quoted_field(Str *out, const char *line, int n) {
    int f = 1;
    const char *start = line;
    const char *p;
    for (p = line; ; p++) {
        if (*p == '"' || *p == '\0') {
            if (f == n) {
                str_add(out, start, (size_t)(p - start));
                return;
            }
            if (*p == '\0') return;
            f++;
            start = p + 1;
        }
    }
}

/* --- the facts ------------------------------------------------------------- */

/* os_release_value -- what `. /etc/os-release && printf %s "${KEY:-}"` gives:
 * the file is SOURCED, so quotes come off and a later assignment wins. */
static void os_release_value(Str *out, const char *key) {
    char *buf;
    size_t len;
    size_t pos = 0;
    Line line;
    size_t klen = strlen(key);

    buf = slurp(env_str("OSR_OS_RELEASE", "/etc/os-release"), &len);
    if (buf == NULL) return;
    while (next_line(buf, len, &pos, &line)) {
        const char *p = line.start;
        size_t remaining = line.len;
        if (remaining <= klen || strncmp(p, key, klen) != 0 || p[klen] != '=') continue;
        p += klen + 1;
        remaining -= klen + 1;
        str_reset(out);
        if (remaining >= 2 && (*p == '"' || *p == '\'') && p[remaining - 1] == *p) {
            str_add(out, p + 1, remaining - 2);  /* the shell strips the quotes */
        } else {
            str_add(out, p, remaining);
        }
    }
    free(buf);
}

static void detect_release(Facts *f) {
    struct stat st;
    const char *path = env_str("OSR_OS_RELEASE", "/etc/os-release");
    if (stat(path, &st) != 0 || access(path, R_OK) != 0) return;  /* [ -r ] */
    os_release_value(&f->distro, "ID");
    os_release_value(&f->id_like, "ID_LIKE");
    os_release_value(&f->codename, "VERSION_CODENAME");
    os_release_value(&f->version_id, "VERSION_ID");
    os_release_value(&f->version, "VERSION");
}

static void detect_arch(Facts *f) {
    struct utsname u;
    if (uname(&u) == 0) str_addz(&f->arch, u.machine);
    if (strcmp(str_text(&f->arch), "x86_64") == 0) str_addz(&f->arch_deb, "amd64");
    else if (strcmp(str_text(&f->arch), "aarch64") == 0) str_addz(&f->arch_deb, "arm64");
    else if (strcmp(str_text(&f->arch), "armv7l") == 0) str_addz(&f->arch_deb, "armhf");
    else str_addz(&f->arch_deb, str_text(&f->arch));   /* unknown -> pass through */
}

/* word_in_list -- the `case " $OSR_DISTRO $OSR_ID_LIKE " in *" debian "*)` test. */
static int word_in_list(const Facts *f, const char *word) {
    Str hay;
    int found;
    str_init(&hay);
    str_addc(&hay, ' ');
    str_addz(&hay, str_text(&f->distro));
    str_addc(&hay, ' ');
    str_addz(&hay, str_text(&f->id_like));
    str_addc(&hay, ' ');
    {
        Str needle;
        str_init(&needle);
        str_addc(&needle, ' ');
        str_addz(&needle, word);
        str_addc(&needle, ' ');
        found = contains(str_text(&hay), str_text(&needle));
        str_free(&needle);
    }
    str_free(&hay);
    return found;
}

/* The binary probe is authoritative (a Debian derivative still has apt-get);
 * the distro/id_like fallback only runs when none of them is installed. */
static void detect_pkg(Facts *f) {
    if (have_cmd("apt-get", NULL))            str_addz(&f->pkg, "apt");
    else if (have_cmd("dnf", NULL))           str_addz(&f->pkg, "dnf");
    else if (have_cmd("pacman", NULL))        str_addz(&f->pkg, "pacman");
    else if (have_cmd("apk", NULL))           str_addz(&f->pkg, "apk");
    else if (have_cmd("xbps-install", NULL))  str_addz(&f->pkg, "xbps");
    else if (have_cmd("emerge", NULL))        str_addz(&f->pkg, "portage");
    else if (word_in_list(f, "debian") || word_in_list(f, "ubuntu")) str_addz(&f->pkg, "apt");
    else if (word_in_list(f, "fedora") || word_in_list(f, "rhel"))   str_addz(&f->pkg, "dnf");
    else if (word_in_list(f, "arch"))    str_addz(&f->pkg, "pacman");
    else if (word_in_list(f, "alpine"))  str_addz(&f->pkg, "apk");
    else if (word_in_list(f, "void"))    str_addz(&f->pkg, "xbps");
    else if (word_in_list(f, "gentoo"))  str_addz(&f->pkg, "portage");
}

/* Probe by evidence, not by PID 1's name: that works inside containers where
 * PID 1 is a shell. */
static void detect_init(Facts *f) {
    if (have_cmd("systemctl", NULL) && dir_exists("/run/systemd/system")) str_addz(&f->init, "systemd");
    else if (have_cmd("rc-service", NULL))                                str_addz(&f->init, "openrc");
    else if (have_cmd("sv", NULL) && dir_exists("/var/service"))          str_addz(&f->init, "runit");
    else if (have_cmd("systemctl", NULL))                                 str_addz(&f->init, "systemd");
    else if (have_cmd("rc-update", NULL))                                 str_addz(&f->init, "openrc");
    else                                                                  str_addz(&f->init, "sysvinit");

    /* System config base dir -- varies by distro FAMILY, not per-package (G7). */
    if (strcmp(str_text(&f->init), "openrc") == 0) str_addz(&f->etc_default, "/etc/conf.d");
    else if (dir_exists("/etc/sysconfig"))         str_addz(&f->etc_default, "/etc/sysconfig");
    else                                           str_addz(&f->etc_default, "/etc/default");
}

/* dt_soc -- the SoC name from the device tree's `compatible` list, "" on a
 * box without one (every PC). The list runs most-specific first
 * ("raspberrypi,4-model-b" then "brcm,bcm2711"), so the LAST entry is the
 * chip; its vendor prefix is dropped and the rest upper-cased: "BCM2711". */
static int dt_soc(Str *out) {
    char *buf;
    size_t len;
    const char *last;
    const char *chip;
    Str path;

    str_init(&path);
    str_addz(&path, env_str("OSR_DEVICETREE", "/proc/device-tree"));
    str_addz(&path, "/compatible");
    buf = slurp(str_text(&path), &len);
    str_free(&path);
    if (buf == NULL) return 0;
    /* NUL-separated, and usually NUL-terminated: step back off the trailer. */
    while (len > 0 && buf[len - 1] == '\0') len--;
    last = buf;
    {
        size_t i;
        for (i = 0; i + 1 < len; i++)
            if (buf[i] == '\0') last = buf + i + 1;
    }
    chip = strchr(last, ',');
    chip = (chip != NULL) ? chip + 1 : last;
    while (*chip != '\0') { str_addc(out, (char)toupper((unsigned char)*chip)); chip++; }
    free(buf);
    return out->len > 0;
}

/* arm_core_name -- the marketing name for a "CPU part" id as /proc/cpuinfo
 * prints it, for ARM's own implementer (0x41). Other implementers (Apple,
 * Qualcomm's own cores) get no table: an unnamed part turns the whole
 * heterogeneous report off rather than printing a raw hex id at somebody. */
static const char *arm_core_name(const char *part) {
    static const char *table[] = {
        "0xd03", "Cortex-A53",  "0xd04", "Cortex-A35",  "0xd05", "Cortex-A55",
        "0xd07", "Cortex-A57",  "0xd08", "Cortex-A72",  "0xd09", "Cortex-A73",
        "0xd0a", "Cortex-A75",  "0xd0b", "Cortex-A76",  "0xd0c", "Neoverse-N1",
        "0xd0d", "Cortex-A77",  "0xd40", "Neoverse-V1", "0xd41", "Cortex-A78",
        "0xd44", "Cortex-X1",   "0xd46", "Cortex-A510", "0xd47", "Cortex-A710",
        "0xd48", "Cortex-X2",   "0xd49", "Neoverse-N2", "0xd4d", "Cortex-A715",
        "0xd4e", "Cortex-X3",   "0xd4f", "Neoverse-V2", "0xd80", "Cortex-A520",
        "0xd81", "Cortex-A720", "0xd82", "Cortex-X4",   NULL, NULL
    };
    int i;
    for (i = 0; table[i] != NULL; i += 2)
        if (strcmp(table[i], part) == 0) return table[i + 1];
    return NULL;
}

/* cpuinfo_field -- the value of `key` on one /proc/cpuinfo line, lower-cased
 * ("CPU part\t: 0xD08" -> "0xd08"), or 0 when the line is a different key. */
static int cpuinfo_field(Str *out, const char *line, const char *key) {
    const char *p = line;
    size_t klen = strlen(key);

    if (strncmp(p, key, klen) != 0) return 0;
    p += klen;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != ':') return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    str_reset(out);
    while (*p != '\0' && *p != ' ' && *p != '\t')
        str_addc(out, (char)tolower((unsigned char)*p++));
    return out->len > 0;
}

/* str_add_ghz -- kHz as the two-decimal GHz everybody quotes: 1800000 ->
 * "1.80GHz". Integer math (no float in this unit), truncating, because a
 * clock rounded up reads as a spec the chip does not have. */
static void str_add_ghz(Str *s, long khz) {
    char buf[64];
    sprintf(buf, "%ld.%02ldGHz", khz / 1000000, (khz % 1000000) / 10000);
    str_addz(s, buf);
}

/* cpu_max_khz -- cpu<n>'s policy maximum, 0 when cpufreq is absent (a VM, a
 * kernel with no scaling driver). $OSR_SYSCPU points the tree elsewhere for
 * the tests. */
static long cpu_max_khz(long cpu) {
    Str path;
    char *buf;
    size_t len;
    long khz = 0;

    str_init(&path);
    str_addz(&path, env_str("OSR_SYSCPU", "/sys/devices/system/cpu"));
    str_addz(&path, "/cpu");
    str_addl(&path, cpu);
    str_addz(&path, "/cpufreq/cpuinfo_max_freq");
    buf = slurp(str_text(&path), &len);
    str_free(&path);
    if (buf != NULL) {
        khz = strtol(buf, NULL, 10);
        free(buf);
    }
    return khz;
}

/* CoreGroup -- one kind of core in a heterogeneous CPU: how many there are
 * and how fast that cluster is allowed to run. Both the ARM and the x86 probe
 * fill these; core_groups_render turns them into the one string a person
 * reads. */
typedef struct {
    const char *name;
    long count;
    long khz;
} CoreGroup;

#define CORE_GROUPS_MAX 8

static void core_groups_render(Str *out, const CoreGroup *g, int n) {
    int i;
    for (i = 0; i < n; i++) {
        if (i > 0) str_addz(out, " + ");
        str_addl(out, g[i].count);
        str_addz(out, "x ");
        str_addz(out, g[i].name);
        /* Per cluster, because the whole point of a hybrid CPU is that the
         * halves do not run at the same speed; one "CPU max MHz" from lscpu
         * would report the fast cluster's for both. */
        if (g[i].khz > 0) {
            str_addz(out, " @ ");
            str_add_ghz(out, g[i].khz);
        }
    }
}

/* group_add -- fold one CPU into the group for `name`, tracking that
 * cluster's highest cpufreq maximum. Returns 0 when there are more kinds of
 * core than anything real ships, which turns the whole report off. */
static int group_add(CoreGroup *g, int *n, const char *name, long cpu) {
    long one;
    int i;

    for (i = 0; i < *n; i++)
        if (strcmp(g[i].name, name) == 0) break;
    if (i == *n) {
        if (*n == CORE_GROUPS_MAX) return 0;
        g[i].name = name;
        g[i].count = 0;
        g[i].khz = 0;
        (*n)++;
    }
    g[i].count++;
    /* Every CPU in a cluster shares one policy, so any member's maximum is the
     * cluster's; take the highest seen in case a board pins one. */
    one = cpu >= 0 ? cpu_max_khz(cpu) : 0;
    if (one > g[i].khz) g[i].khz = one;
    return 1;
}

/* arm_core_groups -- big.LITTLE. lscpu reports ONE "Model name" for a chip
 * whose cores are not all the same part ("Cortex-A55" on an eight-core RK3588
 * that is four A55s and four A76s), so the per-processor "CPU part" lines in
 * /proc/cpuinfo are the only place the mix shows. Returns the number of
 * distinct kinds found, 0 when this is not an ARM chip whose every part the
 * table knows -- a raw hex part id is not an answer to print at somebody. */
static int arm_core_groups(CoreGroup *g) {
    char *info;
    size_t len, pos = 0;
    Line line;
    long cpu = -1;
    int n = 0;
    int arm = 1;
    Str l, val;

    info = slurp(env_str("OSR_CPUINFO", "/proc/cpuinfo"), &len);
    if (info == NULL) return 0;
    str_init(&l);
    str_init(&val);
    while (arm && next_line(info, len, &pos, &line)) {
        const char *name;
        str_reset(&l);
        str_add(&l, line.start, line.len);
        /* "processor : 2" opens each block; the part lines below it are that
         * CPU's, and its number is what indexes the cpufreq tree. */
        if (cpuinfo_field(&val, str_text(&l), "processor")) {
            cpu = strtol(str_text(&val), NULL, 10);
            continue;
        }
        if (cpuinfo_field(&val, str_text(&l), "CPU implementer")) {
            if (strcmp(str_text(&val), "0x41") != 0) arm = 0;
            continue;
        }
        if (!cpuinfo_field(&val, str_text(&l), "CPU part")) continue;
        name = arm_core_name(str_text(&val));
        if (name == NULL) { arm = 0; break; }
        if (!group_add(g, &n, name, cpu)) { arm = 0; break; }
    }
    str_free(&val);
    str_free(&l);
    free(info);
    return arm ? n : 0;
}

/* cpu_has_l3 -- does this CPU see a level-3 cache? On Intel's tiled hybrids
 * (Meteor Lake and up) the two low-power E-cores live on the SoC tile, off
 * the ring and with no L3 at all, which is exactly what makes them LP E-cores
 * rather than ordinary E-cores -- the PMU lists them in cpu_atom with the
 * rest. Missing cache sysfs (a VM) answers "yes", so the split simply does
 * not happen rather than mislabelling every core. */
static int cpu_has_l3(long cpu) {
    int i;
    int seen = 0;

    for (i = 0; i < 10; i++) {
        Str path;
        char *buf;
        size_t len;
        str_init(&path);
        str_addz(&path, env_str("OSR_SYSCPU", "/sys/devices/system/cpu"));
        str_addz(&path, "/cpu");
        str_addl(&path, cpu);
        str_addz(&path, "/cache/index");
        str_addl(&path, (long)i);
        str_addz(&path, "/level");
        buf = slurp(str_text(&path), &len);
        str_free(&path);
        if (buf == NULL) continue;
        seen = 1;
        if (strtol(buf, NULL, 10) == 3) { free(buf); return 1; }
        free(buf);
    }
    return seen ? 0 : 1;
}

/* x86_group -- one PMU device's cpu list ("0-15,24-31" under
 * /sys/devices/cpu_core/cpus) folded in as one kind of core. An absent file
 * is the normal case: only a hybrid chip has these devices at all. */
static void x86_group(CoreGroup *g, int *n, const char *dev, const char *name) {
    Str path;
    char *buf;
    size_t len;
    const char *p;

    str_init(&path);
    str_addz(&path, env_str("OSR_SYSDEV", "/sys/devices"));
    str_addc(&path, '/');
    str_addz(&path, dev);
    str_addz(&path, "/cpus");
    buf = slurp(str_text(&path), &len);
    str_free(&path);
    if (buf == NULL) return;
    /* "0-15,24-31": comma-separated singles and inclusive ranges. */
    p = buf;
    while (*p != '\0') {
        long lo, hi, i;
        if (*p < '0' || *p > '9') { p++; continue; }
        lo = strtol(p, (char **)&p, 10);
        hi = lo;
        if (*p == '-') hi = strtol(p + 1, (char **)&p, 10);
        for (i = lo; i <= hi; i++) {
            /* The atom PMU covers both kinds of small core; the L3 is what
             * tells them apart. */
            const char *kind = name;
            if (strcmp(name, "E-core") == 0 && !cpu_has_l3(i)) kind = "LP E-core";
            if (!group_add(g, n, kind, i)) break;
        }
    }
    free(buf);
}

/* x86_core_groups -- Intel's hybrid chips (12th gen and up). Their brand
 * string says nothing about the split and lscpu prints one model name, but
 * the kernel exposes the two PMUs separately: cpu_core is the P-cores,
 * cpu_atom the E-cores. Same shape as the ARM probe, different evidence. */
static int x86_core_groups(CoreGroup *g) {
    int n = 0;
    x86_group(g, &n, "cpu_core", "P-core");
    x86_group(g, &n, "cpu_atom", "E-core");
    return n;
}

/* cpu_core_types -- the heterogeneous-CPU report, from whichever probe this
 * architecture answers. On ARM the core names ARE the model lscpu printed, so
 * the mix replaces it ("4x Cortex-A55 @ 1.80GHz + 4x Cortex-A76 @ 2.40GHz");
 * on x86 the brand string is the model and the mix is extra, so it is
 * appended. A homogeneous CPU (one group, or no probe) is left alone. */
static void cpu_core_types(Facts *f) {
    CoreGroup g[CORE_GROUPS_MAX];
    int n;

    n = arm_core_groups(g);
    if (n > 1) {
        str_reset(&f->cpu_model);
        core_groups_render(&f->cpu_model, g, n);
        return;
    }
    n = x86_core_groups(g);
    if (n > 1) {
        if (f->cpu_model.len > 0) str_addc(&f->cpu_model, ' ');
        core_groups_render(&f->cpu_model, g, n);
    }
}

/* On an ARM SoC lscpu's "Model name" is the CPU CORE ("Cortex-A72"), not the
 * chip anyone means when they ask what the CPU is. Prefix the chip from the
 * device tree: "BCM2711 Cortex-A72". */
static void cpu_soc_name(Facts *f) {
    Str soc;
    str_init(&soc);
    if (dt_soc(&soc)) {
        if (f->cpu_model.len > 0) { str_addc(&soc, ' '); str_addz(&soc, str_text(&f->cpu_model)); }
        str_reset(&f->cpu_model);
        str_addz(&f->cpu_model, str_text(&soc));
    }
    str_free(&soc);
}

/* cpu_clock -- "CPU max MHz: 1800.0000" -> " @ 1.80GHz", appended only when
 * the model name does not already carry a clock. Intel and most AMD brand
 * strings do ("... CPU @ 3.90GHz"); an ARM core name never does, which is why
 * the Pi printed no speed at all. Integer math on the MHz whole part, so
 * 1800 -> 1.80 and 3893 -> 3.89. On a heterogeneous chip lscpu reports one
 * max, the fastest cluster's -- the honest reading of "how fast is this". */
static void cpu_clock(Facts *f, const char *lscpu) {
    Str mhz;
    long v;

    if (contains(str_text(&f->cpu_model), "GHz") ||
        contains(str_text(&f->cpu_model), "MHz")) return;
    str_init(&mhz);
    if (field_after(&mhz, lscpu, "CPU max MHz:") && mhz.len > 0) {
        v = strtol(str_text(&mhz), NULL, 10);
        if (v > 0) {
            str_addz(&f->cpu_model, " @ ");
            str_add_ghz(&f->cpu_model, v * 1000);
        }
    }
    str_free(&mhz);
}

static void detect_cpu(Facts *f) {
    char *cpu;
    Str tmp;

    str_reset(&f->cpu_arch);
    str_addz(&f->cpu_arch, str_text(&f->arch));
    if (!have_cmd("lscpu", NULL)) {
        cpu_core_types(f);
        cpu_soc_name(f);
        if (f->cpu_cores == 0) f->cpu_cores = f->cpu_threads;
        return;
    }
    cpu = run_capture("lscpu");
    str_init(&tmp);
    field_after(&f->cpu_vendor, cpu, "Vendor ID:");
    /* The model name is the one lscpu field whose INTERIOR whitespace matters:
     * Intel's brand string is a fixed-width field padded with spaces, so
     * "Core(TM) i7-8550U  CPU @ 1.80GHz" arrives with the padding in it and
     * every consumer of OSR_CPU_MODEL prints the gap. Squeezed once, here,
     * rather than at each of the places that print it. */
    str_reset(&tmp);
    if (field_after(&tmp, cpu, "Model name:")) {
        str_reset(&f->cpu_model);
        str_add_squeezed(&f->cpu_model, str_text(&tmp), tmp.len);
    }
    str_reset(&tmp);
    if (field_after(&tmp, cpu, "Architecture:") && tmp.len > 0) {
        str_reset(&f->cpu_arch);
        str_addz(&f->cpu_arch, str_text(&tmp));
    }
    /* `CPU(s)` is the logical count (threads). Physical cores = sockets x
     * cores-per-socket; falls back to threads on a CPU without SMT info. */
    str_reset(&tmp);
    if (field_after(&tmp, cpu, "CPU(s):") && tmp.len > 0) f->cpu_threads = strtol(str_text(&tmp), NULL, 10);
    str_reset(&tmp);
    if (field_after(&tmp, cpu, "Core(s) per socket:") && tmp.len > 0) {
        long per = strtol(str_text(&tmp), NULL, 10);
        long sockets = 1;
        Str s;
        str_init(&s);
        if (field_after(&s, cpu, "Socket(s):") && s.len > 0) sockets = strtol(str_text(&s), NULL, 10);
        str_free(&s);
        f->cpu_cores = per * sockets;
    }
    str_free(&tmp);
    cpu_core_types(f);
    cpu_soc_name(f);
    cpu_clock(f, cpu);
    free(cpu);
    if (f->cpu_cores == 0) f->cpu_cores = f->cpu_threads;
}

/* dmi17 -- _osr_dmi17: type-17 DMI records, or nothing. An unprivileged
 * dmidecode still prints its banner while failing, so emptiness is not a
 * usable signal: demand a real record, or the sudo retry would never fire. */
static char *dmi17(const char *cmd_prefix) {
    Str cmd;
    char *out;
    str_init(&cmd);
    str_addz(&cmd, cmd_prefix);
    str_addz(&cmd, " -t 17");
    out = run_capture(str_text(&cmd));
    str_free(&cmd);
    if (out == NULL) return NULL;
    if (!contains(out, "Memory Device")) { free(out); return NULL; }
    return out;
}

/* ram_edac -- what the memory-controller driver itself knows, from
 * /sys/devices/system/edac/mc. It is the only per-module evidence a box
 * without DMI has: one mc* directory per controller (read as a channel), one
 * dimm* per populated rank, dimm_mem_type naming the technology ("LPDDR4").
 * ARM servers and SoCs with an EDAC driver have it; a Pi has the mc directory
 * with nothing under it, which is what ram_soc is for. */
static void ram_edac(Facts *f) {
    const char *root = env_str("OSR_EDAC", "/sys/devices/system/edac/mc");
    Str p;
    glob_t g;
    size_t i;

    str_init(&p);
    str_addz(&p, root);
    str_addz(&p, "/mc*/dimm*/dimm_mem_type");
    if (glob(str_text(&p), 0, NULL, &g) == 0) {
        for (i = 0; i < g.gl_pathc; i++) {
            char *buf;
            size_t len;
            buf = slurp(g.gl_pathv[i], &len);
            if (buf == NULL) continue;
            buf[strcspn(buf, "\r\n")] = '\0';
            f->ram_sticks++;
            if (f->ram_type.len == 0 && buf[0] != '\0' && strncmp(buf, "Unknown", 7) != 0)
                str_addz(&f->ram_type, buf);
            free(buf);
        }
        globfree(&g);
    }
    str_free(&p);
    if (f->ram_sticks == 0) return;
    str_init(&p);
    str_addz(&p, root);
    str_addz(&p, "/mc*");
    if (glob(str_text(&p), 0, NULL, &g) == 0) {
        f->ram_channels = (long)g.gl_pathc;
        globfree(&g);
    }
    str_free(&p);
}

/* ram_soc -- soldered memory has no DMI record, no SPD and (on a Pi) no EDAC
 * driver: nothing on the running box reports its type or rating. `dmidecode`
 * says "No SMBIOS nor DMI entry point found", lshw prints the size and
 * nothing else, and the firmware reports the SDRAM clock as 0. The SoC fixes
 * both facts, so name them from the chip the device tree already gave us.
 * Datasheet values; a chip that is not in the table is left empty rather than
 * guessed at, and a probe that DID find something is never overwritten.
 * ponytail: a table, not a probe -- add a row when a board turns up. */
static void ram_soc(Facts *f) {
    static const char *table[] = {
        "BCM2835",   "LPDDR2",  "400",
        "BCM2836",   "LPDDR2",  "400",
        "BCM2837",   "LPDDR2",  "900",
        "BCM2837B0", "LPDDR2",  "900",
        "BCM2711",   "LPDDR4",  "3200",
        "BCM2712",   "LPDDR4X", "4267",
        NULL, NULL, NULL
    };
    Str soc;
    int i;

    str_init(&soc);
    if (dt_soc(&soc)) {
        for (i = 0; table[i] != NULL; i += 3) {
            if (strcmp(table[i], str_text(&soc)) != 0) continue;
            if (f->ram_type.len == 0) str_addz(&f->ram_type, table[i + 1]);
            if (f->ram_speed.len == 0) {
                str_addz(&f->ram_speed, table[i + 2]);
                str_addz(&f->ram_speed, "MT/s");
            }
            break;
        }
    }
    str_free(&soc);
}

static void detect_ram(Facts *f) {
    char *mi;
    size_t len;
    char *dmi = NULL;
    Str dmidec;

    str_reset(&f->ram_total);
    str_reset(&f->ram_type);
    str_reset(&f->ram_speed);
    f->ram_sticks = 0;
    f->ram_channels = 0;

    mi = slurp(env_str("OSR_MEMINFO", "/proc/meminfo"), &len);
    if (mi != NULL) {
        Str kb;
        str_init(&kb);
        if (field_after(&kb, mi, "MemTotal:") && kb.len > 0) {
            /* Two decimals, integer math only: MemTotal is always a bit under
             * the installed size (firmware/kernel reservations), so 8GiB reads
             * 7.19 -- showing the fraction makes that visible. */
            long v = strtol(str_text(&kb), NULL, 10);
            char buf[64];
            sprintf(buf, "%ld.%02ldGiB", v / 1048576, (v % 1048576) * 100 / 1048576);
            str_addz(&f->ram_total, buf);
        }
        str_free(&kb);
        free(mi);
    }

    str_init(&dmidec);
    if (have_cmd("dmidecode", &dmidec)) {
        dmi = dmi17(str_text(&dmidec));
        /* Unprivileged: retry through a cached sudo ticket (-n never prompts).
         * Pass the resolved path: sudo's secure_path would otherwise pick a
         * different dmidecode than the one PATH selected. */
        if (dmi == NULL && have_cmd("sudo", NULL)) {
            Str via;
            str_init(&via);
            str_addz(&via, "sudo -n ");
            str_addz(&via, str_text(&dmidec));
            dmi = dmi17(str_text(&via));
            str_free(&via);
        }
    }
    str_free(&dmidec);

    if (dmi != NULL) {
        /* One pass over the type-17 records: a slot counts only once its Size
         * line shows a number ("No Module Installed" = empty slot), and the
         * Type/Speed/Locator lines that follow belong to that populated slot. */
        size_t dlen = strlen(dmi);
        size_t pos = 0;
        Line line;
        int populated = 0;
        long speed = 0;
        Str channels;
        Str field;

        str_init(&channels);
        str_init(&field);
        while (next_line(dmi, dlen, &pos, &line)) {
            Str l;
            const char *t;
            str_init(&l);
            str_add(&l, line.start, line.len);
            t = str_text(&l);
            if (strncmp(t, "Memory Device", 13) == 0) populated = 0;
            {
                Str first;
                const char *rest = t;
                str_init(&first);
                while (*rest == ' ' || *rest == '\t') rest++;
                while (*rest != '\0' && *rest != ' ' && *rest != '\t') str_addc(&first, *rest++);
                while (*rest == ' ' || *rest == '\t') rest++;
                if (strcmp(str_text(&first), "Size:") == 0) {
                    populated = (*rest >= '0' && *rest <= '9');
                    if (populated) f->ram_sticks++;
                } else if (populated && strcmp(str_text(&first), "Type:") == 0 &&
                           f->ram_type.len == 0 &&
                           ((*rest >= 'A' && *rest <= 'Z') || (*rest >= 'a' && *rest <= 'z')) &&
                           strncmp(rest, "Unknown", 7) != 0) {
                    str_reset(&field);
                    trim_field(&field, rest);
                    str_addz(&f->ram_type, str_text(&field));
                } else if (populated && contains(t, "Speed:")) {
                    /* the highest numeric Speed of any populated slot */
                    const char *p = t;
                    while ((p = strstr(p, "Speed:")) != NULL) {
                        const char *v = p + 6;
                        while (*v == ' ' || *v == '\t') v++;
                        if (*v >= '0' && *v <= '9') {
                            long s = strtol(v, NULL, 10);
                            if (s > speed) speed = s;
                        }
                        p += 6;
                    }
                } else if (populated && contains(t, "Locator:")) {
                    const char *p = strstr(t, "Channel");
                    if (p != NULL) {
                        const char *c = p + 7;
                        if (*c == ' ') c++;
                        if ((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') ||
                            (*c >= '0' && *c <= '9')) {
                            Str one;
                            str_init(&one);
                            str_addc(&one, *c);
                            uniq_add(&channels, str_text(&one));
                            str_free(&one);
                        }
                    }
                }
                str_free(&first);
            }
            str_free(&l);
        }
        /* count the distinct channels */
        {
            const char *p = str_text(&channels);
            while (*p != '\0') {
                while (*p == ' ') p++;
                if (*p == '\0') break;
                f->ram_channels++;
                while (*p != '\0' && *p != ' ') p++;
            }
        }
        if (speed > 0) {
            str_addl(&f->ram_speed, speed);
            str_addz(&f->ram_speed, "MT/s");
        }
        str_free(&channels);
        str_free(&field);
        free(dmi);
    }

    /* No DMI, or a DMI that named no module: fall back to the kernel's own
     * memory-controller view, then to what the SoC itself determines. Each
     * only fills what is still empty. */
    if (f->ram_sticks == 0) ram_edac(f);
    if (f->ram_type.len == 0 || f->ram_speed.len == 0) ram_soc(f);
}

/* sysfs_vendor_ids -- the "$dir"/<glob>/device/vendor files, in glob order. */
static void each_vendor_id(const char *dir, const char *pattern,
                           void (*fn)(const char *id, void *ctx), void *ctx) {
    Str p;
    glob_t g;
    size_t i;

    str_init(&p);
    str_addz(&p, dir);
    str_addc(&p, '/');
    str_addz(&p, pattern);
    if (glob(str_text(&p), GLOB_NOCHECK, NULL, &g) == 0) {
        for (i = 0; i < g.gl_pathc; i++) {
            char *buf;
            size_t len;
            if (!file_exists(g.gl_pathv[i])) continue;
            buf = slurp(g.gl_pathv[i], &len);
            if (buf == NULL) continue;
            buf[strcspn(buf, "\r\n")] = '\0';
            fn(buf, ctx);
            free(buf);
        }
        globfree(&g);
    }
    str_free(&p);
}

static void gpu_from_sysfs(const char *id, void *ctx) {
    Facts *f = (Facts *)ctx;
    const char *n = NULL;
    if (strcmp(id, "0x10de") == 0) n = "NVIDIA";
    else if (strcmp(id, "0x1002") == 0) n = "AMD";
    else if (strcmp(id, "0x8086") == 0) n = "Intel";
    else if (strcmp(id, "0x15ad") == 0) n = "VMware";
    else if (strcmp(id, "0x1af4") == 0) n = "QEMU";
    else if (strcmp(id, "0x1414") == 0) n = "Microsoft";
    if (n == NULL) return;
    uniq_add(&f->gpu_vendor, n);
    /* sysfs knows the vendor id, never the codename -- empty chip, which every
     * family classifier reads as "unknown" -> current-gen driver. */
    if (f->gpu_devices.len > 0) str_addc(&f->gpu_devices, '\n');
    str_addz(&f->gpu_devices, n);
    str_addc(&f->gpu_devices, '|');
    f->gpu_count++;
}

/* gpu_from_dt -- a DRM device with no PCI vendor id, named by its device-tree
 * `compatible` ("brcm,bcm2711-vc5"): an SoC GPU. Display and render cores are
 * separate DRM nodes of ONE such GPU (a Pi 4 shows both bcm2711-vc5 and
 * 2711-v3d), so count one and let the display node's name win over the render
 * core's. ponytail: one SoC GPU assumed; nothing ships two. */
static void gpu_from_dt(const char *compat, void *ctx) {
    Facts *f = (Facts *)ctx;
    const char *tag = norm_gpu(compat);
    const char *chip = strchr(compat, ',');

    if (strcmp(tag, "Unknown") == 0) return;
    chip = (chip != NULL) ? chip + 1 : compat;
    if (f->gpu_count > 0) {
        if (contains(chip, "v3d") || !contains(str_text(&f->gpu_devices), "v3d")) return;
        str_reset(&f->gpu_vendor);
        str_reset(&f->gpu_model);
        str_reset(&f->gpu_devices);
    }
    uniq_add(&f->gpu_vendor, tag);
    str_addz(&f->gpu_model, tag);
    str_addc(&f->gpu_model, ' ');
    str_addz(&f->gpu_model, chip);
    str_addz(&f->gpu_devices, tag);
    str_addc(&f->gpu_devices, '|');
    str_addz(&f->gpu_devices, chip);
    f->gpu_count = 1;
}

static void npu_from_sysfs(const char *id, void *ctx) {
    Facts *f = (Facts *)ctx;
    const char *n;
    if (strcmp(id, "0x8086") == 0) n = "Intel";
    else if (strcmp(id, "0x1022") == 0 || strcmp(id, "0x1002") == 0) n = "AMD";
    else if (strcmp(id, "0x10de") == 0) n = "NVIDIA";
    else n = "Unknown";
    uniq_add(&f->npu_vendor, n);
    f->npu_count++;
}

/* lspci_lines -- `lspci -mm | grep -E "<classes>"`, or NULL. */
static char *lspci_lines(void) {
    if (!have_cmd("lspci", NULL)) return NULL;
    return run_capture("lspci -mm");
}

static void detect_gpu(Facts *f) {
    char *out;

    str_reset(&f->gpu_vendor);
    str_reset(&f->gpu_model);
    str_reset(&f->gpu_devices);
    f->gpu_count = 0;

    out = lspci_lines();
    if (out != NULL) {
        size_t len = strlen(out);
        size_t pos = 0;
        Line line;
        while (next_line(out, len, &pos, &line)) {
            Str l;
            Str vendor;
            Str dev;
            Str model;
            const char *tag;
            str_init(&l);
            str_add(&l, line.start, line.len);
            if (!contains(str_text(&l), "VGA compatible controller") &&
                !contains(str_text(&l), "3D controller")) {
                str_free(&l);
                continue;
            }
            str_init(&vendor);
            str_init(&dev);
            quoted_field(&vendor, str_text(&l), 4);
            quoted_field(&dev, str_text(&l), 6);
            {
                Str both;
                str_init(&both);
                str_addz(&both, str_text(&vendor));
                str_addc(&both, ' ');
                str_addz(&both, str_text(&dev));
                tag = norm_gpu(str_text(&both));
                str_free(&both);
            }
            uniq_add(&f->gpu_vendor, tag);

            /* lspci names a chip by codename with the marketing name bracketed
             * ("Kaby Lake-S GT2 [HD Graphics 630]") -- the bracket is the part
             * anyone recognizes, so prefer it and fall back to the whole
             * string. Don't stutter "Intel Intel Arc A770". */
            str_init(&model);
            {
                const char *open = strchr(str_text(&dev), '[');
                const char *close = open != NULL ? strchr(open, ']') : NULL;
                if (open != NULL && close != NULL) str_add(&model, open + 1, (size_t)(close - open - 1));
                else str_addz(&model, str_text(&dev));
            }
            if (strncmp(str_text(&model), tag, strlen(tag)) != 0) {
                Str prefixed;
                str_init(&prefixed);
                str_addz(&prefixed, tag);
                str_addc(&prefixed, ' ');
                str_addz(&prefixed, str_text(&model));
                str_free(&model);
                model = prefixed;
            }
            if (f->gpu_model.len > 0) str_addz(&f->gpu_model, ", ");
            str_addz(&f->gpu_model, str_text(&model));

            /* The codename is what is left of the bracket ("Navi 22 [Radeon RX
             * 6700 XT]" -> "Navi 22"); unbracketed strings are already one. */
            {
                Str chip;
                const char *sp = strstr(str_text(&dev), " [");
                str_init(&chip);
                if (sp != NULL) str_add(&chip, str_text(&dev), (size_t)(sp - str_text(&dev)));
                else str_addz(&chip, str_text(&dev));
                if (f->gpu_devices.len > 0) str_addc(&f->gpu_devices, '\n');
                str_addz(&f->gpu_devices, tag);
                str_addc(&f->gpu_devices, '|');
                str_addz(&f->gpu_devices, str_text(&chip));
                str_free(&chip);
            }
            f->gpu_count++;
            str_free(&model);
            str_free(&vendor);
            str_free(&dev);
            str_free(&l);
        }
        free(out);
    }

    if (f->gpu_vendor.len == 0) {
        each_vendor_id(env_str("OSR_DRM", "/sys/class/drm"), "card*/device/vendor",
                       gpu_from_sysfs, f);
    }
    if (f->gpu_vendor.len == 0) {
        each_vendor_id(env_str("OSR_DRM", "/sys/class/drm"), "card*/device/of_node/compatible",
                       gpu_from_dt, f);
    }
}

static void detect_npu(Facts *f) {
    str_reset(&f->npu_vendor);
    f->npu_count = 0;
    each_vendor_id(env_str("OSR_ACCEL", "/sys/class/accel"), "accel*/device/vendor",
                   npu_from_sysfs, f);
    if (f->npu_vendor.len == 0) {
        char *out = lspci_lines();
        if (out != NULL) {
            size_t len = strlen(out);
            size_t pos = 0;
            Line line;
            while (next_line(out, len, &pos, &line)) {
                Str l;
                Str vendor;
                Str dev;
                Str both;
                str_init(&l);
                str_add(&l, line.start, line.len);
                if (!contains(str_text(&l), "Processing accelerators")) { str_free(&l); continue; }
                str_init(&vendor);
                str_init(&dev);
                quoted_field(&vendor, str_text(&l), 4);
                quoted_field(&dev, str_text(&l), 6);
                str_init(&both);
                str_addz(&both, str_text(&vendor));
                str_addc(&both, ' ');
                str_addz(&both, str_text(&dev));
                uniq_add(&f->npu_vendor, norm_gpu(str_text(&both)));
                f->npu_count++;
                str_free(&both);
                str_free(&vendor);
                str_free(&dev);
                str_free(&l);
            }
            free(out);
        }
    }
}

static void detect_virt(Facts *f) {
    str_reset(&f->virt);
    str_addz(&f->virt, "none");
    if (have_cmd("systemd-detect-virt", NULL)) {
        char *v = run_capture("systemd-detect-virt");
        if (v != NULL) {
            Str t;
            str_init(&t);
            trim_field(&t, v);
            str_trim_trailing(&t, '\n');
            if (t.len > 0) {
                str_reset(&f->virt);
                str_addz(&f->virt, str_text(&t));
            }
            str_free(&t);
            free(v);
        }
    }
    if (strcmp(str_text(&f->virt), "none") == 0 && have_cmd("lscpu", NULL)) {
        char *cpu = run_capture("lscpu");
        if (cpu != NULL) {
            /* the sh grep was case-insensitive over four patterns, then a case
             * over the matching text */
            if (contains(cpu, "VMware")) { str_reset(&f->virt); str_addz(&f->virt, "vmware"); }
            else if (contains(cpu, "VirtualBox")) { str_reset(&f->virt); str_addz(&f->virt, "virtualbox"); }
            else if (contains(cpu, "KVM")) { str_reset(&f->virt); str_addz(&f->virt, "kvm"); }
            else if (contains(cpu, "QEMU")) { str_reset(&f->virt); str_addz(&f->virt, "qemu"); }
            free(cpu);
        }
    }
}

/* --- output ---------------------------------------------------------------- */

/* --- publishing the facts --------------------------------------------------
 *
 * A fact reaches its consumer one of two ways, and both must name the same set:
 * printed as a shell assignment for `eval` (what lib/detect.sh does), or set in
 * this process's environment for the runner and every child it forks. So the
 * emitters take a SINK and the two spellings are one list, not two -- a fact
 * added to one is added to both, which is the drift this shape exists to
 * prevent. Sink itself is declared above the split, since both bodies publish
 * through it.
 */
static void sh_sink(void *ctx, const char *name, const char *value) {
    sh_assign((Str *)ctx, name, value);
}
static void env_sink(void *ctx, const char *name, const char *value) {
    (void)ctx;
    osr_setenv(name, value);
}

static void emit_num(Sink put, void *ctx, const char *name, long v) {
    Str s;
    str_init(&s);
    str_addl(&s, v);
    put(ctx, name, str_text(&s));
    str_free(&s);
}

static void emit_cpu(Sink put, void *ctx, const Facts *f) {
    put(ctx, "OSR_CPU_VENDOR", str_text(&f->cpu_vendor));
    put(ctx, "OSR_CPU_MODEL", str_text(&f->cpu_model));
    put(ctx, "OSR_CPU_ARCH", str_text(&f->cpu_arch));
    emit_num(put, ctx, "OSR_CPU_CORES", f->cpu_cores);
    emit_num(put, ctx, "OSR_CPU_THREADS", f->cpu_threads);
}

static void emit_ram(Sink put, void *ctx, const Facts *f) {
    put(ctx, "OSR_RAM_TOTAL", str_text(&f->ram_total));
    put(ctx, "OSR_RAM_TYPE", str_text(&f->ram_type));
    put(ctx, "OSR_RAM_SPEED", str_text(&f->ram_speed));
    emit_num(put, ctx, "OSR_RAM_STICKS", f->ram_sticks);
    emit_num(put, ctx, "OSR_RAM_CHANNELS", f->ram_channels);
}

static void emit_gpu(Sink put, void *ctx, const Facts *f) {
    put(ctx, "OSR_GPU_VENDOR", str_text(&f->gpu_vendor));
    put(ctx, "OSR_GPU_MODEL", str_text(&f->gpu_model));
    emit_num(put, ctx, "OSR_GPU_COUNT", f->gpu_count);
    put(ctx, "OSR_GPU_DEVICES", str_text(&f->gpu_devices));
}

static void emit_npu(Sink put, void *ctx, const Facts *f) {
    put(ctx, "OSR_NPU_VENDOR", str_text(&f->npu_vendor));
    emit_num(put, ctx, "OSR_NPU_COUNT", f->npu_count);
}

/* emit_release -- the ten os-release/arch facets, which `all` alone publishes. */
static void emit_release(Sink put, void *ctx, const Facts *f) {
    put(ctx, "OSR_DISTRO", str_text(&f->distro));
    put(ctx, "OSR_PKG", str_text(&f->pkg));
    put(ctx, "OSR_INIT", str_text(&f->init));
    put(ctx, "OSR_CODENAME", str_text(&f->codename));
    put(ctx, "OSR_VERSION_ID", str_text(&f->version_id));
    put(ctx, "OSR_VERSION", str_text(&f->version));
    put(ctx, "OSR_ID_LIKE", str_text(&f->id_like));
    put(ctx, "OSR_ARCH", str_text(&f->arch));
    put(ctx, "OSR_ARCH_DEB", str_text(&f->arch_deb));
    put(ctx, "OSR_ETC_DEFAULT", str_text(&f->etc_default));
}

/* detect_all -- every probe, in the order `osr detect all` runs them, published
 * through `put`. Returns 0 when no package manager was found, which is the one
 * line osr_detect printed. */
static int detect_all(Sink put, void *ctx, Facts *f) {
    detect_release(f);
    detect_arch(f);
    detect_pkg(f);
    detect_init(f);
    emit_release(put, ctx, f);
    detect_cpu(f);
    emit_cpu(put, ctx, f);
    detect_ram(f);
    emit_ram(put, ctx, f);
    detect_gpu(f);
    emit_gpu(put, ctx, f);
    detect_npu(f);
    emit_npu(put, ctx, f);
    detect_virt(f);
    put(ctx, "OSR_VIRT", str_text(&f->virt));
    return f->pkg.len != 0;
}

/* osr_detect_export -- osr_detect, straight into this process's environment.
 * `what` is "all" or one probe's name ("ram", the one the runner re-runs after
 * warming a sudo ticket, because the DMI half of it needs root). */
void osr_detect_export(const char *what) {
    Facts f;

    facts_init(&f);
    if (strcmp(what, "all") == 0) {
        if (!detect_all(env_sink, NULL, &f))
            osr_warnf("could not detect a package manager (OSR_DISTRO='%s')",
                      str_text(&f.distro));
    } else if (strcmp(what, "cpu") == 0) {
        detect_arch(&f); detect_cpu(&f); emit_cpu(env_sink, NULL, &f);
    } else if (strcmp(what, "ram") == 0) {
        detect_ram(&f); emit_ram(env_sink, NULL, &f);
    } else if (strcmp(what, "gpu") == 0) {
        detect_gpu(&f); emit_gpu(env_sink, NULL, &f);
    } else if (strcmp(what, "npu") == 0) {
        detect_npu(&f); emit_npu(env_sink, NULL, &f);
    } else if (strcmp(what, "virt") == 0) {
        detect_virt(&f);
        osr_setenv("OSR_VIRT", str_text(&f.virt));
    }
}

/* cmd_gpu_chip -- osr_gpu_chip: the chip codename of the first <vendor> GPU,
 * "" when unknown (no lspci, or a device lspci could not name). Reads
 * OSR_GPU_DEVICES from the environment, where osr_detect exported it. */
int osr_gpu_chip(Str *out, const char *vendor) {
    const char *devices = env_str("OSR_GPU_DEVICES", "");
    size_t len = strlen(devices);
    size_t pos = 0;
    Line line;
    size_t vlen = strlen(vendor);
    int found = 0;

    while (!found && next_line(devices, len, &pos, &line)) {
        if (line.len >= vlen && strncmp(line.start, vendor, vlen) == 0 && line.start[vlen] == '|') {
            str_add(out, line.start + vlen + 1, line.len - vlen - 1);
            found = 1;
        }
    }
    return found;
}

static int cmd_gpu_chip(const char *vendor) {
    Str out;
    str_init(&out);
    /* awk's `print` -- and nothing at all when no row matched. */
    if (osr_gpu_chip(&out, vendor)) str_addc(&out, '\n');
    out_flush(&out);
    str_free(&out);
    return 0;
}

static int usage(void) {
    fputs("usage: osr detect <subcommand>\n\n", stderr);
    fputs("  all                every fact, as shell assignments\n", stderr);
    fputs("  cpu | ram | gpu | npu | virt   one probe's facts\n", stderr);
    fputs("  gpu-chip <vendor>  the chip codename of the first such GPU\n", stderr);
    return 2;
}

int osr_detect_main(int argc, char **argv) {
    Facts f;
    Str out;
    const char *what;

    if (argc < 2) return usage();
    what = argv[1];
    if (strcmp(what, "gpu-chip") == 0 && argc == 3) return cmd_gpu_chip(argv[2]);

    facts_init(&f);
    str_init(&out);

    if (strcmp(what, "all") == 0) {
        int found = detect_all(sh_sink, &out, &f);
        out_flush(&out);
        str_free(&out);
        /* `[ -n "$OSR_PKG" ] || warn ...` -- the one line osr_detect printed. */
        if (!found)
            osr_warnf("could not detect a package manager (OSR_DISTRO='%s')",
                      str_text(&f.distro));
        return 0;
    }
    if (strcmp(what, "cpu") == 0) {
        detect_arch(&f);
        detect_cpu(&f);
        emit_cpu(sh_sink, &out, &f);
    } else if (strcmp(what, "ram") == 0) {
        detect_ram(&f);
        emit_ram(sh_sink, &out, &f);
    } else if (strcmp(what, "gpu") == 0) {
        detect_gpu(&f);
        emit_gpu(sh_sink, &out, &f);
    } else if (strcmp(what, "npu") == 0) {
        detect_npu(&f);
        emit_npu(sh_sink, &out, &f);
    } else if (strcmp(what, "virt") == 0) {
        detect_virt(&f);
        sh_assign(&out, "OSR_VIRT", str_text(&f.virt));
    } else {
        str_free(&out);
        return usage();
    }
    out_flush(&out);
    str_free(&out);
    return 0;
}

#else /* _WIN32 */

/* --- the Windows body ------------------------------------------------------
 *
 * The same job and the same variable names, out of a different place. A Linux
 * box answers "what am I" out of /etc/os-release, /proc and /sys; a Windows one
 * answers it out of the registry and a handful of system calls, and there is
 * no lspci to walk.
 *
 * What matters is that the FACET NAMES are identical, because they are what
 * lib/pkgmap's `name@facet` keys are matched against (DESIGN section 1a) and
 * what a rice's `require:` predicates read (section 10). So:
 *
 *   OSR_DISTRO      windows
 *   OSR_VERSION_ID  the product major, 11 or 10       (~ a distro's version)
 *   OSR_CODENAME    DisplayVersion, e.g. 24H2         (~ trixie, noble)
 *   OSR_ARCH        x86_64 | arm64 | x86              (the same three names)
 *   OSR_PKG         windows -- which names the map, lib/pkgmap/windows.map,
 *                   exactly as `apt` names apt.map. Not scoop/choco/winget:
 *                   which of those serves a package is a per-ROW decision,
 *                   not a property of the machine.
 *   OSR_INIT        scm, the one service manager there is
 *
 * The hardware facets (section 7) are what a rice reports and preflights
 * against, so they are answered too, from the registry and
 * GlobalMemoryStatusEx. There is no GPU or NPU probe yet: the honest answer is
 * "not detected", and a wrong vendor is worse than none. Every consumer
 * already handles that, because a Linux box with no lspci gives it too.
 * ------------------------------------------------------------------------ */

#define OSR_WINVER_KEY "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"
#define OSR_CPU_KEY    "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"

#ifndef PROCESSOR_ARCHITECTURE_ARM64
#define PROCESSOR_ARCHITECTURE_ARM64 12
#endif

/* reg -- one REG_SZ into a Str, empty when it is not there. lib/pkg.c owns the
 * reader (it reads the same hive to refresh the environment after an install);
 * this is the Str shape the emitters want. */
static void reg(Str *out, const char *subkey, const char *value) {
    char buf[512];
    str_reset(out);
    if (osr_reg_read_str(HKEY_LOCAL_MACHINE, subkey, value, buf, sizeof(buf))) {
        str_addz(out, buf);
    }
}

static void detect_release(Facts *f) {
    Str tmp;

    str_init(&tmp);
    str_addz(&f->distro, "windows");
    str_addz(&f->pkg, "windows");
    str_addz(&f->init, "scm");

    /* DisplayVersion is the modern name (21H2 onward); ReleaseId is what
     * older builds wrote instead. Either one is the codename analogue. */
    reg(&tmp, OSR_WINVER_KEY, "DisplayVersion");
    if (tmp.len == 0) reg(&tmp, OSR_WINVER_KEY, "ReleaseId");
    str_add(&f->codename, str_text(&tmp), tmp.len);

    /* Windows 11 still reports major version 10, so the build number is the
     * only honest way to tell the two apart: 22000 is the threshold. */
    reg(&tmp, OSR_WINVER_KEY, "CurrentBuild");
    str_addz(&f->version_id, (tmp.len > 0 && atoi(str_text(&tmp)) >= 22000) ? "11" : "10");

    /* OSR_VERSION is the full one on the POSIX side (VERSION= from
     * os-release); here that is the build, which is what a support answer
     * ever actually asks for. */
    str_add(&f->version, str_text(&tmp), tmp.len);

    str_free(&tmp);
}

static void detect_arch(Facts *f) {
    SYSTEM_INFO si;

    /* GetNativeSystemInfo, not GetSystemInfo: under WOW64 the latter reports
     * the EMULATED architecture, which is the wrong facet to key a package
     * choice on -- an arm64 machine would ask for the x86_64 row and get a
     * binary it can only emulate. Names match the POSIX branch's OSR_ARCH. */
    GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: str_addz(&f->arch, "x86_64"); break;
        case PROCESSOR_ARCHITECTURE_ARM64: str_addz(&f->arch, "arm64");  break;
        case PROCESSOR_ARCHITECTURE_INTEL: str_addz(&f->arch, "x86");    break;
        default: break;
    }
    /* No dpkg here, so no Debian architecture name to carry. */
}

static void detect_cpu(Facts *f) {
    SYSTEM_INFO si;
    Str tmp;

    str_init(&tmp);
    /* The brand string, out of the registry key every tool reads it from.
     * Squeezed because Intel pads its own brand string internally -- see
     * str_add_squeezed, which exists for this one field. */
    reg(&tmp, OSR_CPU_KEY, "ProcessorNameString");
    str_add_squeezed(&f->cpu_model, str_text(&tmp), tmp.len);
    reg(&tmp, OSR_CPU_KEY, "VendorIdentifier");
    str_add(&f->cpu_vendor, str_text(&tmp), tmp.len);
    str_add(&f->cpu_arch, str_text(&f->arch), f->arch.len);
    str_free(&tmp);

    /* dwNumberOfProcessors is logical processors, i.e. threads. The physical
     * core count needs GetLogicalProcessorInformation and is left at 0 rather
     * than guessed: a benchmark that divides by a wrong core count reports a
     * wrong number confidently. */
    GetNativeSystemInfo(&si);
    f->cpu_threads = (long)si.dwNumberOfProcessors;
}

static void detect_ram(Facts *f) {
    MEMORYSTATUSEX mem;

    mem.dwLength = sizeof(mem);
    if (!GlobalMemoryStatusEx(&mem)) return;

    /* Whole GB, rounded, which is the unit and the rounding the POSIX branch
     * gives MemTotal -- so the two report one number the same way. The type,
     * speed, stick and channel counts come from SMBIOS, which is what
     * dmidecode reads on the other side and what nothing here reads yet. */
    str_addl(&f->ram_total, (long)((mem.ullTotalPhys + (1024UL * 1024UL * 512UL))
                                   / (1024UL * 1024UL * 1024UL)));
    str_addz(&f->ram_total, "G");
}

/* detect_virt -- is this a virtual machine? Left unanswered rather than
 * guessed: the reliable tells (hypervisor CPUID leaf, SMBIOS manufacturer)
 * are each a probe of their own, and "" is exactly what systemd-detect-virt
 * gives on a machine it cannot classify. */
static void detect_virt(Facts *f) { (void)f; }

static void sh_sink(void *ctx, const char *name, const char *value) {
    sh_assign((Str *)ctx, name, value);
}
static void env_sink(void *ctx, const char *name, const char *value) {
    (void)ctx;
    osr_setenv(name, value);
}

static void emit_num(Sink put, void *ctx, const char *name, long v) {
    Str s;
    str_init(&s);
    str_addl(&s, v);
    put(ctx, name, str_text(&s));
    str_free(&s);
}

/* detect_all -- every probe, published through `put`. The same variable set
 * the POSIX branch publishes, including the ones this side leaves empty: a
 * consumer must see the name whatever the answer, or "unset" and "no such
 * device" become the same thing. */
static int detect_all(Sink put, void *ctx, Facts *f) {
    detect_release(f);
    detect_arch(f);
    put(ctx, "OSR_DISTRO", str_text(&f->distro));
    put(ctx, "OSR_PKG", str_text(&f->pkg));
    put(ctx, "OSR_INIT", str_text(&f->init));
    put(ctx, "OSR_CODENAME", str_text(&f->codename));
    put(ctx, "OSR_VERSION_ID", str_text(&f->version_id));
    put(ctx, "OSR_VERSION", str_text(&f->version));
    put(ctx, "OSR_ID_LIKE", str_text(&f->id_like));
    put(ctx, "OSR_ARCH", str_text(&f->arch));
    put(ctx, "OSR_ARCH_DEB", str_text(&f->arch_deb));
    put(ctx, "OSR_ETC_DEFAULT", str_text(&f->etc_default));

    detect_cpu(f);
    put(ctx, "OSR_CPU_VENDOR", str_text(&f->cpu_vendor));
    put(ctx, "OSR_CPU_MODEL", str_text(&f->cpu_model));
    put(ctx, "OSR_CPU_ARCH", str_text(&f->cpu_arch));
    emit_num(put, ctx, "OSR_CPU_CORES", f->cpu_cores);
    emit_num(put, ctx, "OSR_CPU_THREADS", f->cpu_threads);

    detect_ram(f);
    put(ctx, "OSR_RAM_TOTAL", str_text(&f->ram_total));
    put(ctx, "OSR_RAM_TYPE", str_text(&f->ram_type));
    put(ctx, "OSR_RAM_SPEED", str_text(&f->ram_speed));
    emit_num(put, ctx, "OSR_RAM_STICKS", f->ram_sticks);
    emit_num(put, ctx, "OSR_RAM_CHANNELS", f->ram_channels);

    put(ctx, "OSR_GPU_VENDOR", str_text(&f->gpu_vendor));
    put(ctx, "OSR_GPU_MODEL", str_text(&f->gpu_model));
    emit_num(put, ctx, "OSR_GPU_COUNT", f->gpu_count);
    put(ctx, "OSR_GPU_DEVICES", str_text(&f->gpu_devices));
    put(ctx, "OSR_NPU_VENDOR", str_text(&f->npu_vendor));
    emit_num(put, ctx, "OSR_NPU_COUNT", f->npu_count);

    detect_virt(f);
    put(ctx, "OSR_VIRT", str_text(&f->virt));
    return f->pkg.len != 0;
}

void osr_detect_export(const char *what) {
    Facts f;

    /* Nothing here is expensive or privileged, so there is no reason to probe
     * one facet at a time -- the POSIX branch splits them because its RAM
     * probe needs a sudo ticket the runner has not warmed yet. */
    (void)what;
    facts_init(&f);
    detect_all(env_sink, NULL, &f);
}

/* osr_gpu_chip -- no GPU probe on this side, so the honest answer is "no such
 * device". A module branching on a chip codename gets the same 0 a Linux box
 * with no lspci gives it, which is a path they all already handle. */
int osr_gpu_chip(Str *out, const char *vendor) {
    (void)out;
    (void)vendor;
    return 0;
}

static int usage(void) {
    fputs("usage: osr detect <what>\n\n", stderr);
    fputs("  all              every fact, as shell assignments to eval\n", stderr);
    fputs("  gpu-chip <v>     the chip codename of the first <v> GPU\n", stderr);
    return 2;
}

int osr_detect_main(int argc, char **argv) {
    Facts f;
    Str out;

    if (argc < 2) return usage();
    if (strcmp(argv[1], "gpu-chip") == 0 && argc == 3) return 1;   /* never detected */
    if (strcmp(argv[1], "all") != 0) return usage();

    facts_init(&f);
    str_init(&out);
    detect_all(sh_sink, &out, &f);
    out_flush(&out);
    str_free(&out);
    return 0;
}

#endif /* _WIN32 */
