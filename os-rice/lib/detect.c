/* lib/detect.c -- the C behind lib/detect.sh: detect the host once.
 *
 *   all          every fact, as shell assignments to eval
 *   ram          just the RAM facets (install.sh re-probes them after sudo)
 *   cpu|gpu|npu|virt   the individual probes, same shape
 *   gpu-chip <vendor>  the chip codename of the first <vendor> GPU
 *
 * Sets OSR_DISTRO/OSR_PKG/OSR_INIT plus the release, arch and config-path
 * facets the map @qualifier resolver (§1) and the preconditions (§10) read,
 * then the hardware facets (§7): CPU id, RAM, GPU/NPU vendor, virtualization.
 *
 * The probes are the sh ones, command for command: lscpu, lspci -mm,
 * dmidecode -t 17 (with the unprivileged sudo -n retry), /proc/meminfo,
 * /sys/class/drm, /sys/class/accel, systemd-detect-virt. Same order, same
 * fallbacks, same "silent and command-guarded so a minimal box never errors"
 * rule -- and the same override knobs (OSR_MEMINFO, OSR_DRM, OSR_ACCEL), which
 * is what makes any of it testable.
 *
 * Byte-for-byte with the sh original, frozen at test/ref/detect_sh_ref.sh and
 * diffed by test/unit/detect_c_parity.sh.
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

#include "common.h"
#include "cmds.h"

#include <glob.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

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

static void detect_cpu(Facts *f) {
    char *cpu;
    Str tmp;

    str_reset(&f->cpu_arch);
    str_addz(&f->cpu_arch, str_text(&f->arch));
    if (!have_cmd("lscpu", NULL)) {
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
 * prevent.
 */
typedef void (*Sink)(void *ctx, const char *name, const char *value);

static void sh_sink(void *ctx, const char *name, const char *value) {
    sh_assign((Str *)ctx, name, value);
}
static void env_sink(void *ctx, const char *name, const char *value) {
    (void)ctx;
    setenv(name, value, 1);
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
        setenv("OSR_VIRT", str_text(&f.virt), 1);
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
