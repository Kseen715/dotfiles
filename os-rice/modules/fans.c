/* modules/fans.c -- quiet fan curves, keyed on a temperature that can be
 * trusted.
 *
 * THE NOISE THIS EXISTS TO FIX: a Super I/O chip (NCT6779D, IT87, ...) drives
 * four or five fan headers, and the BIOS leaves the ones it was never told
 * about in "full on" -- pwm<N>_enable = 0, pwm<N> = 255, forever, at idle, in
 * a room nobody is in. The kernel exposes the same headers through hwmon the
 * moment the right driver is loaded, and a curve over them is the difference
 * between a box you can sit next to and one you cannot.
 *
 * WHICH CHANNELS THIS MODULE TOUCHES, and why only those: exactly the ones
 * reading pwm<N>_enable = 0, i.e. pinned at full speed with nobody steering
 * them. A channel already on manual (1) was set by a person, and one on the
 * chip's own Smart Fan modes (2..5) is a curve the firmware runs without a
 * userspace daemon alive to keep it honest -- taking either over would be this
 * module overruling a decision it did not make.
 *
 * WHICH TEMPERATURE STEERS IT, and why the board's own sensors do not:
 * a curve is only as good as the number it reads, and Super I/O board sensors
 * are routinely garbage. The machine this was written for (HUANANZHI X79,
 * NCT6779D) reports SYSTIN 120C, CPUTIN 127.5C and AUXTIN0 17.5C on an idle
 * box whose CPU is actually at 48C -- and its unmanaged channels are wired to
 * AUXTIN0, so the chip's own Smart Fan IV over them would be a curve over a
 * number that means nothing. So the steering temperature must come from a CPU
 * die sensor (coretemp, k10temp, zenpower, cpu_thermal), and when there is no
 * such sensor this module REFUSES rather than guessing. A fan curve keyed on a
 * lie is worse than a loud fan.
 *
 * WHY fancontrol AND NOT THE CHIP'S OWN AUTO POINTS: the chip can only key its
 * curve on a temperature the chip itself can see, which is the set just
 * dismissed. Steering by the CPU die means a userspace loop, and fancontrol
 * (lm-sensors) is that loop, already packaged everywhere and already fail-open
 * -- on exit or on SIGTERM it restores the channels to full speed, so a
 * crashed daemon is loud, never hot.
 *
 * WHY /etc/modules-load.d: the Super I/O driver is not auto-bound. nct6775
 * matches a chip found by probing a legacy I/O port, not by a bus ID, so
 * nothing loads it for you -- and a fancontrol config naming an hwmon that
 * does not exist yet is a service that fails at boot. The driver behind every
 * channel taken over is written there, so the next boot has it.
 *
 * THE CURVE (all dials are env, defaults below):
 *
 *   <= MINTEMP (45C)  ->  MINPWM (38/255, ~15%) -- audible only if you listen
 *      MAXTEMP (75C)  ->  255. Linear between.
 *   MINSTART 100      -- the kick a stalled fan needs to start turning again
 *   MINSTOP  38       -- the lowest duty that keeps it turning once started
 *
 * That is deliberately silence-first: a box that idles at 48C spends its life
 * at the floor, and the ramp only arrives once the die is genuinely warm.
 *
 * NO TACHOMETER IS NOT AN ERROR: three-pin fans on a four-pin header, and
 * splitters, leave fan<N>_input reading 0. fancontrol uses the tacho only to
 * verify spin-up, so a channel without a live one is configured without an
 * FCFANS entry rather than skipped -- it just cannot be checked.
 *
 * C89 + POSIX.
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Super I/O chips carry at most this many fan headers; the loop below is over
 * channel numbers, so it needs a ceiling rather than a discovery pass. */
#define FAN_MAX_CH 8

/* A hwmon whose pwm channels this module may take over. */
typedef struct {
    Str dir;      /* <hwmon root>/hwmonN */
    Str base;     /* "hwmonN" -- the name /etc/fancontrol addresses it by */
    Str name;     /* the hwmon name: nct6779, it8728, ... */
    Str devpath;  /* "devices/platform/nct6775.2576", for fancontrol's DEVPATH */
    Str driver;   /* the kernel module behind it, "" when it is not one */
    int ch[FAN_MAX_CH];   /* channel numbers reading pwm<n>_enable = 0 */
    int tach[FAN_MAX_CH]; /* 1 where fan<n>_input reports a live reading */
    int nch;
} Ctl;

/* The temperature the curve is steered by. */
typedef struct {
    Str dir, base, name, devpath;
    int temp;     /* the temp<N>_input to read, 0 when none was found */
} Src;

/* CPU die sensors, in preference order. Everything NOT in this list -- every
 * super-I/O board sensor -- is untrusted on purpose; see the header. */
static const char *const trusted[] = {
    "coretemp", "k10temp", "zenpower", "k8temp", "cpu_thermal", NULL
};

static const char *hwmon_root(void)  { return env_str("OSR_HWMON_DIR", "/sys/class/hwmon"); }
static const char *fc_conf(void)     { return env_str("OSR_FANCONTROL_CONF", "/etc/fancontrol"); }
static const char *modload_conf(void) {
    return env_str("OSR_MODULES_LOAD_CONF", "/etc/modules-load.d/99-osr-fans.conf");
}

static long min_temp(void)  { return env_long("OSR_FAN_MINTEMP",  45); }
static long max_temp(void)  { return env_long("OSR_FAN_MAXTEMP",  75); }
static long min_pwm(void)   { return env_long("OSR_FAN_MINPWM",   38); }
static long max_pwm(void)   { return env_long("OSR_FAN_MAXPWM",  255); }
static long min_start(void) { return env_long("OSR_FAN_MINSTART", 100); }
static long min_stop(void)  { return env_long("OSR_FAN_MINSTOP",  38); }
static long interval(void)  { return env_long("OSR_FAN_INTERVAL", 10); }

/* --- reading sysfs --------------------------------------------------------
 * Every file here is one short line. read_text strips the trailing newline the
 * way a `$(cat ...)` would; absent is "" rather than an error, because half of
 * these attributes are optional on any given chip.
 * ------------------------------------------------------------------------- */

static int read_text(const char *path, Str *out) {
    char *buf;
    size_t len;

    buf = slurp(path, &len);
    if (buf == NULL) return 0;
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'
                       || buf[len - 1] == ' ' || buf[len - 1] == '\t')) len--;
    str_add(out, buf, len);
    free(buf);
    return 1;
}

static long read_long(const char *path, long dflt) {
    Str s;
    long v = dflt;

    str_init(&s);
    if (read_text(path, &s) && str_text(&s)[0] != '\0') v = atol(str_text(&s));
    str_free(&s);
    return v;
}

/* attr -- "<dir>/<kind><n><suffix>", the one string shape every read below
 * needs. n <= 0 leaves the number out. The buffer is reset first so one Str
 * can walk a whole scan. */
static void attr(Str *out, const char *dir, const char *kind, int n,
                 const char *suffix) {
    str_free(out);
    str_init(out);
    str_addzz(out, dir, "/", kind, (const char *)NULL);
    if (n > 0) str_addl(out, (long)n);
    if (suffix != NULL) str_addz(out, suffix);
}

/* sysfs_rel -- what a symlink under a hwmon really points at, as fancontrol
 * wants it: relative to /sys, so "devices/platform/coretemp.0". Everything
 * before "/devices/" is dropped, which is what makes the same code work
 * against a fixture tree rooted somewhere else entirely. */
static void sysfs_rel(Str *out, const char *link) {
    char resolved[OSR_PATH_MAX];
    const char *at;

    if (!osr_absolute_dir(link, resolved, sizeof resolved)) return;
    at = strstr(resolved, "/devices/");
    str_addz(out, at != NULL ? at + 1 : resolved);
}

/* base_name -- the last path component, which for <hwmon>/device/driver is the
 * kernel module's name. */
static void base_name(Str *out, const char *path) {
    char resolved[OSR_PATH_MAX];
    const char *slash;

    if (!osr_absolute_dir(path, resolved, sizeof resolved)) return;
    slash = strrchr(resolved, '/');
    str_addz(out, slash != NULL ? slash + 1 : resolved);
}

static void ctl_init(Ctl *c) {
    str_initv(&c->dir, &c->base, &c->name, &c->devpath, &c->driver, (Str *)NULL);
    c->nch = 0;
}
static void ctl_free(Ctl *c) {
    str_freev(&c->dir, &c->base, &c->name, &c->devpath, &c->driver, (Str *)NULL);
}
static void src_init(Src *s) {
    str_initv(&s->dir, &s->base, &s->name, &s->devpath, (Str *)NULL);
    s->temp = 0;
}
static void src_free(Src *s) {
    str_freev(&s->dir, &s->base, &s->name, &s->devpath, (Str *)NULL);
}

static int is_trusted(const char *name) {
    int i;
    for (i = 0; trusted[i] != NULL; i++)
        if (strcmp(name, trusted[i]) == 0) return 1;
    return 0;
}

/* pick_temp -- the temp<N>_input to steer by within a trusted hwmon. The
 * package sensor is the right one where there is one (it is the hottest die
 * reading and the one that moves first); otherwise the lowest-numbered input,
 * which is temp1 on every driver in the list. */
static int pick_temp(const char *dir) {
    Str p, label;
    int n, found = 0;

    str_initv(&p, &label, (Str *)NULL);
    for (n = 1; n <= 32 && !found; n++) {
        attr(&p, dir, "temp", n, "_label");
        str_free(&label);
        str_init(&label);
        if (!read_text(str_text(&p), &label)) continue;
        if (strstr(str_text(&label), "Package id") == NULL) continue;
        attr(&p, dir, "temp", n, "_input");
        if (file_exists(str_text(&p))) found = n;
    }
    for (n = 1; n <= 32 && !found; n++) {
        attr(&p, dir, "temp", n, "_input");
        if (file_exists(str_text(&p))) found = n;
    }
    str_freev(&p, &label, (Str *)NULL);
    return found;
}

/* scan_one -- fill `c` and/or `s` from one hwmonN directory. A hwmon can be
 * both (a chip with fan headers and a usable temperature), so these are two
 * questions asked of the same directory, not a choice between them. */
static void scan_one(const char *dir, const char *base, Ctl *c, Src *s) {
    Str p, name;
    int n;

    str_initv(&p, &name, (Str *)NULL);
    attr(&p, dir, "name", 0, NULL);
    (void)read_text(str_text(&p), &name);

    if (s->temp == 0 && is_trusted(str_text(&name))) {
        int t = pick_temp(dir);
        if (t > 0) {
            s->temp = t;
            str_addz(&s->dir, dir);
            str_addz(&s->base, base);
            str_addz(&s->name, str_text(&name));
            attr(&p, dir, "device", 0, NULL);
            sysfs_rel(&s->devpath, str_text(&p));
        }
    }

    if (c->nch == 0) {
        int ch[FAN_MAX_CH], tach[FAN_MAX_CH], nch = 0;
        for (n = 1; n <= FAN_MAX_CH; n++) {
            attr(&p, dir, "pwm", n, NULL);
            if (!file_exists(str_text(&p))) continue;
            attr(&p, dir, "pwm", n, "_enable");
            /* 0 is "full on, nobody steering". Everything else -- manual,
             * the chip's own modes, or a driver that reports -1 for "no
             * control at all" (nouveau on some cards) -- is left alone. */
            if (read_long(str_text(&p), -1) != 0) continue;
            attr(&p, dir, "fan", n, "_input");
            ch[nch] = n;
            tach[nch] = read_long(str_text(&p), 0) > 0 ? 1 : 0;
            nch++;
        }
        if (nch > 0) {
            memcpy(c->ch, ch, sizeof ch);
            memcpy(c->tach, tach, sizeof tach);
            c->nch = nch;
            str_addz(&c->dir, dir);
            str_addz(&c->base, base);
            str_addz(&c->name, str_text(&name));
            attr(&p, dir, "device", 0, NULL);
            sysfs_rel(&c->devpath, str_text(&p));
            attr(&p, dir, "device/driver", 0, NULL);
            base_name(&c->driver, str_text(&p));
        }
    }
    str_freev(&p, &name, (Str *)NULL);
}

static void scan(Ctl *c, Src *s) {
    Str list, dir;
    size_t pos = 0;
    Line line;

    str_initv(&list, &dir, (Str *)NULL);
    osr_list_dir(&list, hwmon_root(), NULL, NULL);
    while (next_line(str_text(&list), list.len, &pos, &line)) {
        Str base;
        if (line.len < 6 || strncmp(line.start, "hwmon", 5) != 0) continue;
        str_init(&base);
        str_add(&base, line.start, line.len);
        str_free(&dir);
        str_init(&dir);
        str_addzz(&dir, hwmon_root(), "/", str_text(&base), (const char *)NULL);
        scan_one(str_text(&dir), str_text(&base), c, s);
        str_free(&base);
    }
    str_freev(&list, &dir, (Str *)NULL);
}

/* load_superio -- nothing auto-binds a Super I/O driver: the chip is found by
 * probing a legacy I/O port, so the module has to be asked for by name. These
 * two cover the chips on consumer and workstation boards; a modprobe for a
 * chip that is not there fails cleanly and costs one fork. */
static void load_superio(void) {
    static const char *const drivers[] = { "nct6775", "it87", NULL };
    char *argv[4];
    int i;

    for (i = 0; drivers[i] != NULL; i++) {
        argv[0] = (char *)"modprobe";
        argv[1] = (char *)"-q";
        argv[2] = (char *)drivers[i];
        argv[3] = NULL;
        (void)osr_run_root_quiet(argv);
    }
}

/* --- the config -----------------------------------------------------------
 * /etc/fancontrol is one line per setting, each holding a
 * `<hwmon>/pwm<n>=<value>` entry per channel. per_channel writes one such
 * line; the whole file is these plus the two device lines that let fancontrol
 * re-find the hwmons after a reboot renumbers them.
 * ------------------------------------------------------------------------- */

static void per_channel(Str *out, const char *key, const Ctl *c, long value) {
    int i;
    str_addzz(out, key, "=", (const char *)NULL);
    for (i = 0; i < c->nch; i++) {
        if (i > 0) str_addc(out, ' ');
        str_addzz(out, str_text(&c->base), "/pwm", (const char *)NULL);
        str_addl(out, (long)c->ch[i]);
        str_addc(out, '=');
        str_addl(out, value);
    }
    str_addc(out, '\n');
}

static void conf_text(Str *out, const Ctl *c, const Src *s) {
    int i, same = strcmp(str_text(&c->base), str_text(&s->base)) == 0;

    str_addz(out, "# managed by os-rice (modules/fans.c) -- regenerate with "
                  "`osr module run fans`\n"
                  "# Only channels that were pinned at full speed "
                  "(pwm<n>_enable = 0) are steered here;\n"
                  "# the curve reads ");
    str_addzz(out, str_text(&s->name), ", a CPU die sensor, because this "
                   "board's super-I/O\n# temperatures cannot be trusted.\n",
              (const char *)NULL);
    str_addz(out, "INTERVAL=");
    str_addl(out, interval());
    str_addc(out, '\n');

    str_addzz(out, "DEVPATH=", str_text(&c->base), "=", str_text(&c->devpath),
              (const char *)NULL);
    if (!same)
        str_addzz(out, " ", str_text(&s->base), "=", str_text(&s->devpath),
                  (const char *)NULL);
    str_addc(out, '\n');

    str_addzz(out, "DEVNAME=", str_text(&c->base), "=", str_text(&c->name),
              (const char *)NULL);
    if (!same)
        str_addzz(out, " ", str_text(&s->base), "=", str_text(&s->name),
                  (const char *)NULL);
    str_addc(out, '\n');

    str_addz(out, "FCTEMPS=");
    for (i = 0; i < c->nch; i++) {
        if (i > 0) str_addc(out, ' ');
        str_addzz(out, str_text(&c->base), "/pwm", (const char *)NULL);
        str_addl(out, (long)c->ch[i]);
        str_addzz(out, "=", str_text(&s->base), "/temp", (const char *)NULL);
        str_addl(out, (long)s->temp);
        str_addz(out, "_input");
    }
    str_addc(out, '\n');

    /* Only channels with a live tachometer: fancontrol uses it to verify
     * spin-up, and pointing it at a header that always reads 0 would make it
     * declare a healthy fan dead and run everything at full speed. */
    str_addz(out, "FCFANS=");
    {
        int first = 1;
        for (i = 0; i < c->nch; i++) {
            if (!c->tach[i]) continue;
            if (!first) str_addc(out, ' ');
            first = 0;
            str_addzz(out, str_text(&c->base), "/pwm", (const char *)NULL);
            str_addl(out, (long)c->ch[i]);
            str_addzz(out, "=", str_text(&c->base), "/fan", (const char *)NULL);
            str_addl(out, (long)c->ch[i]);
            str_addz(out, "_input");
        }
    }
    str_addc(out, '\n');

    per_channel(out, "MINTEMP",  c, min_temp());
    per_channel(out, "MAXTEMP",  c, max_temp());
    per_channel(out, "MINSTART", c, min_start());
    per_channel(out, "MINSTOP",  c, min_stop());
    per_channel(out, "MINPWM",   c, min_pwm());
    per_channel(out, "MAXPWM",   c, max_pwm());
}

/* same_text -- `[ "$(cat file 2>/dev/null)" = "$(...)" ]`: trailing newlines
 * do not count, the way a command substitution drops them. */
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

/* The apply half, in one step so a half-written config is never left behind a
 * spinner that already said [ok]. */
typedef struct { const Ctl *c; const Src *s; } Apply;

static int apply_conf(void *ctx) {
    const Apply *a = (const Apply *)ctx;
    Str body, dir;
    char *argv[4];

    /* /etc/modules-load.d is not guaranteed to exist (it does not on a Void
     * box), and `tee` into a missing directory fails with a bare ENOENT that
     * reads like a permissions problem. */
    if (str_text(&a->c->driver)[0] != '\0') {
        const char *path = modload_conf();
        const char *slash = strrchr(path, '/');
        str_init(&dir);
        if (slash == NULL)      str_addc(&dir, '.');
        else if (slash == path) str_addc(&dir, '/');
        else                    str_add(&dir, path, (size_t)(slash - path));
        argv[0] = (char *)"mkdir"; argv[1] = (char *)"-p";
        argv[2] = (char *)str_text(&dir); argv[3] = NULL;
        (void)osr_run_root(argv);
        str_free(&dir);

        str_init(&body);
        str_addz(&body, "# managed by os-rice (modules/fans.c): the super-I/O "
                        "driver behind the fan\n# headers is probed by port, "
                        "not by bus id, so nothing loads it on its own.\n");
        str_addzz(&body, str_text(&a->c->driver), "\n", (const char *)NULL);
        (void)osr_write_root(modload_conf(), str_text(&body));
        str_free(&body);
    }

    str_init(&body);
    conf_text(&body, a->c, a->s);
    (void)osr_write_root(fc_conf(), str_text(&body));
    str_free(&body);
    return 1;
}

int osrm_fans(void) {
    static const char *const pkgs[] = { "lm_sensors", "fancontrol", NULL };
    const char *virt = env_str("OSR_VIRT", "none");
    Ctl c;
    Src s;
    Apply a;
    Str msg, want;
    int ok = 1, changed;

    if (osr_theme_only()) return osr_theme_only_skip("fans");

    /* A guest has no fans: the host's hwmon is not passed through, and on the
     * rare setup where it is, the host owns that policy. */
    if (strcmp(virt, "none") != 0) {
        osr_infof("fans: %s guest - fans belong to the host, skipping", virt);
        return 1;
    }

    ctl_init(&c);
    src_init(&s);
    scan(&c, &s);
    if (c.nch == 0) {
        /* Nothing found may only mean the driver was never asked for. */
        load_superio();
        ctl_free(&c); src_free(&s);
        ctl_init(&c); src_init(&s);
        scan(&c, &s);
    }

    if (c.nch == 0) {
        osr_info("fans: no fan header is pinned at full speed - nothing to "
                 "quieten (a chip curve or a manual setting already owns them)");
        ok = 1;
        goto out;
    }
    if (s.temp == 0) {
        osr_warnf("fans: %s has %d unmanaged fan header(s), but this machine "
                  "exposes no CPU die sensor (coretemp/k10temp/zenpower/"
                  "cpu_thermal) - refusing to build a curve on super-I/O board "
                  "temperatures, which read 120C on a cold board",
                  str_text(&c.name), c.nch);
        ok = 1;
        goto out;
    }

    str_initv(&msg, &want, (Str *)NULL);
    str_addzz(&msg, "fans: ", str_text(&c.name), " pwm", (const char *)NULL);
    {
        int i;
        for (i = 0; i < c.nch; i++) {
            if (i > 0) str_addc(&msg, ',');
            str_addl(&msg, (long)c.ch[i]);
        }
    }
    str_addzz(&msg, " pinned full -> curve on ", str_text(&s.name), "/temp",
              (const char *)NULL);
    str_addl(&msg, (long)s.temp);
    str_addz(&msg, "_input: ");
    str_addl(&msg, min_pwm());
    str_addz(&msg, "/255 up to ");
    str_addl(&msg, min_temp());
    str_addz(&msg, "C, 255/255 at ");
    str_addl(&msg, max_temp());
    str_addc(&msg, 'C');
    osr_info(str_text(&msg));

    a.c = &c;
    a.s = &s;
    conf_text(&want, &c, &s);
    changed = !same_text(fc_conf(), str_text(&want));

    ok = osr_pkg_install_step("Installing fancontrol", pkgs);
    if (changed) {
        ok = osr_step("Writing the fan curve", apply_conf, &a) && ok;
    } else {
        osr_info("fans: the curve is already the one this module writes, "
                 "leaving it alone");
    }
    if (!osr_service_enable("fancontrol"))
        osr_warn("could not enable fancontrol (needs a real init)");
    /* A running daemon reads the config once, at start. */
    if (changed && strcmp(osr_mod_init(), "systemd") == 0) {
        char *argv[4];
        argv[0] = (char *)"systemctl"; argv[1] = (char *)"restart";
        argv[2] = (char *)"fancontrol"; argv[3] = NULL;
        (void)osr_run_root(argv);
    }
    str_freev(&msg, &want, (Str *)NULL);

out:
    ctl_free(&c);
    src_free(&s);
    return ok;
}
