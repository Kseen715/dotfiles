/* lib/modules.c -- the registry: every module written in C, on either system.
 *
 * A rice manifest names modules and the runner runs each one. A module is
 * modules/<name>.c exporting `int osrm_<name>(void)`, registered in the one
 * table below, and everything it may call is lib/module.h.
 *
 * ONE TABLE. There used to be two -- this one, and a second at the repository
 * root that the Windows core dispatched through with a different signature
 * (repo_root, themes_root, map_path, theme, theme_only). That second table
 * existed because the Windows core had no module runtime to read those from;
 * lib/module.c is that runtime now, on both systems, so a module reads its
 * context through osr_mod_* like every other and the two tables are one.
 *
 * WHICH ROWS ARE GUARDED, and why guarded rather than split: a module that
 * only one system can run is still one file in modules/ with an empty branch
 * for the other (modules/win-tweaks.c, modules/flameshot.c). The guard here is
 * about the REGISTRY, and it says something narrower -- do not offer this name
 * on a system where running it would do nothing. `osr module has flameshot`
 * answering yes on Windows and then installing nothing is worse than answering
 * no, because the runner's next move depends on the answer.
 *
 * The `session` field is the C form of a .sh module's `# session:` first line
 * (x11 / wayland / x11+wayland), which is what lets `grep -l` answer "what
 * breaks if I move this rice to Wayland" without reading every module. Windows
 * rows carry `windows`, which is the same kind of statement.
 *
 *   osr module list           every module, one per line
 *   osr module has <name>     exit 0 when this build owns that name
 *   osr module session <name> its session marker
 *   osr module themable <name>  exit 0 when it consumes the theme
 *   osr module run <name>     install it
 *   osr module run --theme-only <name>   only its theme layer (section 6a)
 *
 * C89 + POSIX, and C89 + Win32.
 */
#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "common.h"
#include "cmds.h"
#include "module.h"

/* One row per module. Keep it alphabetical: it is also the listing order. */
typedef struct {
    const char *name;
    const char *session;
    int themable;              /* does it read the resolved theme at all? */
    int (*run)(void);
} ModuleRow;

/* Runtime builds keep this table as the authoritative POSIX module catalog but
 * do not create references to module objects. Static builds retain the direct
 * function pointers and therefore still need no compiler at execution time. */
#ifdef OSR_RUNTIME_MODULES
#define MODULE_RUN(fn) NULL
#else
#define MODULE_RUN(fn) fn
#endif

/* The module list, once. Each row is name, the osrm_ function's suffix, the
 * session marker, and whether the module reads the resolved theme. The
 * prototypes and the table below both expand from here, so a module cannot be
 * declared and left out of the registry, or listed with a prototype that does
 * not exist. Keep each list alphabetical: it is also the listing order.
 *
 * Rows appearing in both lists are the modules both systems have. */
#define OSR_MODULES_WIN(X) \
    X("fastfetch",   fastfetch,   "windows", 1) \
    X("oh-my-posh",  oh_my_posh,  "windows", 1) \
    X("osrvv",       osrvv,       "windows", 0) \
    X("pwsh",        pwsh,        "windows", 0) \
    X("starship",    starship,    "windows", 1) \
    X("wezterm",     wezterm,     "windows", 1) \
    X("win-debloat", win_debloat, "windows", 0) \
    X("win-tweaks",  win_tweaks,  "windows", 0) \
    X("win-update",  win_update,  "windows", 0) \
    X("win-winutil", win_winutil, "windows", 0)

#define OSR_MODULES_POSIX(X) \
    X("alacritty",       alacritty,       "x11+wayland", 1) \
    X("amacc",           amacc,           "x11+wayland", 0) \
    X("amnezia-vpn",     amnezia_vpn,     "x11+wayland", 0) \
    X("arandr",          arandr,          "x11",         0) \
    X("archives",        archives,        "x11+wayland", 0) \
    X("arocc",           arocc,           "x11+wayland", 0) \
    X("audio",           audio,           "x11+wayland", 0) \
    X("avahi",           avahi,           "x11+wayland", 0) \
    X("benchmark",       benchmark,       "x11+wayland", 0) \
    X("blueman",         blueman,         "x11+wayland", 0) \
    X("brightnessctl",   brightnessctl,   "x11+wayland", 0) \
    X("btop",            btop,            "x11+wayland", 1) \
    X("celluloid",       celluloid,       "x11+wayland", 0) \
    X("cliphist",        cliphist,        "wayland",     1) \
    X("codecs",          codecs,          "x11+wayland", 0) \
    X("copyq",           copyq,           "x11",         1) \
    X("cproc",           cproc,           "x11+wayland", 0) \
    X("cpu-microcodes",  cpu_microcodes,  "x11+wayland", 0) \
    X("cuik",            cuik,            "x11+wayland", 0) \
    X("curseforge",      curseforge,      "x11+wayland", 0) \
    X("datagrip",        datagrip,        "x11+wayland", 0) \
    X("discord",         discord,         "x11+wayland", 0) \
    X("disks",           disks,           "x11+wayland", 0) \
    X("dkms",            dkms,            "x11+wayland", 0) \
    X("dnscrypt",        dnscrypt,        "x11+wayland", 0) \
    X("docker",          docker,          "x11+wayland", 0) \
    X("dunst",           dunst,           "x11+wayland", 1) \
    X("easyeffects",     easyeffects,     "x11+wayland", 0) \
    X("evolution",       evolution,       "x11+wayland", 1) \
    X("fastfetch",       fastfetch,       "x11+wayland", 1) \
    X("fcitx5",          fcitx5,          "x11+wayland", 1) \
    X("feh",             feh,             "x11",         0) \
    X("firefox",         firefox,         "x11+wayland", 1) \
    X("flameshot",       flameshot,       "x11",         0) \
    X("flatpak",         flatpak,         "x11+wayland", 0) \
    X("foot",            foot,            "wayland",     1) \
    X("gh",              gh,              "x11+wayland", 0) \
    X("ghostty",         ghostty,         "x11+wayland", 1) \
    X("git-base",        git_base,        "x11+wayland", 0) \
    X("gnome-focus",     gnome_focus,     "wayland",     0) \
    X("gnome-overview",  gnome_overview,  "x11+wayland", 0) \
    X("gnome-panel",     gnome_panel,     "x11+wayland", 1) \
    X("go",              go,              "x11+wayland", 0) \
    X("gpaste",          gpaste,          "x11+wayland", 0) \
    X("gpu-drivers",     gpu_drivers,     "x11+wayland", 0) \
    X("gtklock",         gtklock,         "wayland",     1) \
    X("gvfs",            gvfs,            "x11+wayland", 0) \
    X("helpers",         helpers,         "x11+wayland", 0) \
    X("helvum",          helvum,          "x11+wayland", 0) \
    X("htop",            htop,            "x11+wayland", 0) \
    X("hyprcursor",      hyprcursor,      "wayland",     0) \
    X("hypridle",        hypridle,        "wayland",     1) \
    X("hyprland",        hyprland,        "wayland",     1) \
    X("hyprlock",        hyprlock,        "wayland",     1) \
    X("hyprpaper",       hyprpaper,       "wayland",     1) \
    X("hyprpicker",      hyprpicker,      "wayland",     0) \
    X("i3",              i3,              "x11",         1) \
    X("i3lock",          i3lock,          "x11",         1) \
    X("input",           input,           "x11",         0) \
    X("inxi",            inxi,            "x11+wayland", 0) \
    X("kate",            kate,            "x11+wayland", 1) \
    X("kdeconnect",      kdeconnect,      "x11+wayland", 0) \
    X("keyring",         keyring,         "x11+wayland", 0) \
    X("lacc",            lacc,            "x11+wayland", 0) \
    X("lcc",             lcc,             "x11+wayland", 0) \
    X("lightdm",         lightdm,         "x11",         1) \
    X("logging",         logging,         "x11+wayland", 0) \
    X("loupe",           loupe,           "x11+wayland", 0) \
    X("luminance",       luminance,       "wayland",     0) \
    X("mako",            mako,            "wayland",     1) \
    X("micro",           micro,           "x11+wayland", 1) \
    X("mirrors",         mirrors,         "x11+wayland", 0) \
    X("nautilus",        nautilus,        "x11+wayland", 0) \
    X("ncdu",            ncdu,            "x11+wayland", 0) \
    X("networkmanager",  networkmanager,  "x11+wayland", 0) \
    X("nwg-displays",    nwg_displays,    "wayland",     0) \
    X("obs-studio",      obs_studio,      "x11+wayland", 0) \
    X("onlyoffice",      onlyoffice,      "x11+wayland", 0) \
    X("openssh",         openssh,         "x11+wayland", 0) \
    X("osrvv",           osrvv,           "x11+wayland", 0) \
    X("pacman-multilib", pacman_multilib, "x11+wayland", 0) \
    X("paru",            paru,            "x11+wayland", 0) \
    X("picom",           picom,           "x11",         1) \
    X("pipewire",        pipewire,        "x11+wayland", 0) \
    X("plasma",          plasma,          "x11+wayland", 0) \
    X("polkit-agent",    polkit_agent,    "x11+wayland", 0) \
    X("polybar",         polybar,         "x11",         1) \
    X("power",           power,           "x11+wayland", 0) \
    X("printer",         printer,         "x11+wayland", 0) \
    X("proteus",         proteus,         "x11+wayland", 0) \
    X("pulseaudio",      pulseaudio,      "x11+wayland", 0) \
    X("qbittorrent",     qbittorrent,     "x11+wayland", 0) \
    X("qpwgraph",        qpwgraph,        "x11+wayland", 0) \
    X("redshift",        redshift,        "x11",         0) \
    X("rofi",            rofi,            "x11",         1) \
    X("rust",            rust,            "x11+wayland", 0) \
    X("sddm",            sddm,            "x11+wayland", 1) \
    X("serie",           serie,           "x11+wayland", 1) \
    X("shecc",           shecc,           "x11+wayland", 0) \
    X("smallerc",        smallerc,        "x11+wayland", 0) \
    X("starship",        starship,        "x11+wayland", 1) \
    X("steam",           steam,           "x11+wayland", 0) \
    X("swap",            swap,            "x11+wayland", 0) \
    X("swaylock",        swaylock,        "wayland",     1) \
    X("tcc",             tcc,             "x11+wayland", 0) \
    X("telegram",        telegram,        "x11+wayland", 1) \
    X("theming",         theming,         "x11",         1) \
    X("thumbnails",      thumbnails,      "x11+wayland", 0) \
    X("thunar",          thunar,          "x11+wayland", 0) \
    X("thunderbird",     thunderbird,     "x11+wayland", 1) \
    X("ufw",             ufw,             "x11+wayland", 0) \
    X("viewers",         viewers,         "x11+wayland", 1) \
    X("vlc",             vlc,             "x11+wayland", 1) \
    X("vmware-init",     vmware_init,     "x11",         0) \
    X("vscode",          vscode,          "x11+wayland", 0) \
    X("vscode-insiders", vscode_insiders, "x11+wayland", 0) \
    X("waybar",          waybar,          "wayland",     1) \
    X("waydroid",        waydroid,        "wayland",     0) \
    X("wayland",         wayland,         "wayland",     0) \
    X("waylock",         waylock,         "wayland",     1) \
    X("weston-rdp",      weston_rdp,      "wayland",     0) \
    X("wezterm",         wezterm,         "x11+wayland", 1) \
    X("wleave",          wleave,          "wayland",     1) \
    X("wlogout",         wlogout,         "wayland",     1) \
    X("wofi",            wofi,            "wayland",     1) \
    X("xcc",             xcc,             "x11+wayland", 0) \
    X("xdg",             xdg,             "x11+wayland", 0) \
    X("xorg",            xorg,            "x11",         1) \
    X("yandex-browser",  yandex_browser,  "x11+wayland", 0) \
    X("yazi",            yazi,            "x11+wayland", 1) \
    X("zen-browser",     zen_browser,     "x11+wayland", 1) \
    X("zig",             zig,             "x11+wayland", 0) \
    X("zip",             zip,             "x11+wayland", 0) \
    X("zsh",             zsh,             "x11+wayland", 1)

#ifdef _WIN32
#define OSR_MODULES(X) OSR_MODULES_WIN(X)
#else
#define OSR_MODULES(X) OSR_MODULES_POSIX(X)
#endif

#define X(name, fn, session, themable) int osrm_##fn(void);
OSR_MODULES(X)
#undef X

static const ModuleRow modules[] = {
#define X(name, fn, session, themable) { name, session, themable, MODULE_RUN(osrm_##fn) },
    OSR_MODULES(X)
#undef X
};
#define MODULE_COUNT (sizeof(modules) / sizeof(modules[0]))

static const ModuleRow *find(const char *name);

/* osr_module_themable -- "does installing this module need a theme?".
 *
 * Only a module that reads $OSR_THEME/$OSR_THEME_DIR has anything to do with
 * the answer, so only those make install.sh ask the question. Asking it for
 * `osr module benchmark` -- a package install with no appearance at all -- put
 * a theme picker in front of a benchmark, which is what this exists to stop.
 *
 * A C module carries the flag in its row. A .sh module carries it as the
 * `# themable: yes` header beside `# session:`, and the marker is authoritative
 * rather than inferred: test/unit/module_themable.sh diffs every marker against
 * what the script actually references, so a module that grows a theme layer and
 * forgets the header fails the suite instead of silently losing its paint. */
/* osr_module_has -- does this tier own that name? */
int osr_module_has(const char *name) { return find(name) != NULL; }

/* osr_module_run -- run one module, in this process.
 *
 * theme_only is the §6a pass: everything that installs, downloads, builds or
 * starts becomes a no-op for the rest of this process, so what the module does
 * is its file copying -- which is what a theme is. The sh tier spelled the same
 * thing osr_apply_stub_mutators (lib/apply.sh); see lib/module.h on why one is
 * derived and one is enumerated.
 *
 * Returns 1 for success. A failing module is reported and the caller decides
 * whether to continue -- one broken module must not abort a whole rice install,
 * same contract the sh run_module had. */
int osr_module_run(const char *name, int theme_only) {
    const ModuleRow *m;

    if (theme_only) osr_set_theme_only(1);
    m = find(name);
    if (m == NULL) {
        osr_warn("no such C module");
        return 0;
    }
#ifdef OSR_RUNTIME_MODULES
    return osr_module_runtime_run(name);
#else
    return m->run();
#endif
}

int osr_module_themable(const char *name) {
    Str path;
    char *buf;
    size_t len, pos = 0;
    Line line;
    int themable = 0;
    const ModuleRow *m;

    m = find(name);
    if (m != NULL) return m->themable;

    str_init(&path);
    str_addzz(&path, env_str("OSR_ROOT", "."), "/modules/", name, ".sh", (const char *)NULL);
    buf = slurp(str_text(&path), &len);
    str_free(&path);
    if (buf == NULL) return 0;

    /* The header block only: a `# themable:` further down is prose, not a
     * marker, and the whole point is that the answer is cheap to find. */
    while (next_line(buf, len, &pos, &line)) {
        if (line.len == 0 || line.start[0] != '#') break;
        if (line.len > 11 && memcmp(line.start, "# themable:", 11) == 0) {
            const char *v = line.start + 11;
            size_t n = line.len - 11;
            while (n > 0 && is_space(*v)) { v++; n--; }
            themable = (n >= 3 && memcmp(v, "yes", 3) == 0);
            break;
        }
    }
    free(buf);
    return themable;
}

/* osr_module_names -- every C module's name, appended to out one per line.
 * install.sh's `--list-modules` merges these with the shell scripts. */
void osr_module_names(Str *out) {
    size_t i;
    for (i = 0; i < MODULE_COUNT; i++) {
        str_addzz(out, modules[i].name, "\n", (const char *)NULL);
    }
}

static const ModuleRow *find(const char *name) {
    size_t i;
    for (i = 0; i < MODULE_COUNT; i++) {
        if (strcmp(modules[i].name, name) == 0) return &modules[i];
    }
    return NULL;
}

static int usage(void) {
    fputs("usage: osr module <subcommand> [name]\n\n", stderr);
    fputs("  list              every module this tier implements\n", stderr);
    fputs("  has <name>        exit 0 when it does implement <name>\n", stderr);
    fputs("  session <name>    its `# session:` marker\n", stderr);
    fputs("  themable <name>   exit 0 when it consumes the resolved theme\n", stderr);
    fputs("  run <name>        install it\n", stderr);
    fputs("  run --theme-only <name>  only its theme layer, no installs\n", stderr);
    fputs("  pkgmap <name>     what lib/pkgmap resolves that name to\n", stderr);
    return 2;
}

int osr_module_main(int argc, char **argv) {
    if (argc < 2) return usage();

    if (strcmp(argv[1], "list") == 0 && argc == 2) {
        Str out;
        str_init(&out);
        osr_module_names(&out);
        out_flush(&out);
        str_free(&out);
        return 0;
    }
    if (strcmp(argv[1], "pkgmap") == 0 && argc == 3) {
        /* the resolver by itself, so a test can diff it against pkg.sh's
         * _pkgmap_one without installing anything */
        Str out;
        str_init(&out);
        osr_pkgmap_resolve(&out, argv[2]);
        out_flush(&out);
        str_free(&out);
        return 0;
    }
    if (strcmp(argv[1], "has") == 0 && argc == 3) {
        return find(argv[2]) != NULL ? 0 : 1;
    }
    if (strcmp(argv[1], "session") == 0 && argc == 3) {
        const ModuleRow *m = find(argv[2]);
        if (m == NULL) return 1;
        printf("%s\n", m->session);
        return 0;
    }
    if (strcmp(argv[1], "themable") == 0 && argc == 3) {
        return osr_module_themable(argv[2]) ? 0 : 1;
    }
    if (strcmp(argv[1], "run") == 0 && (argc == 3 || argc == 4)) {
        int theme_only = argc == 4;
        if (theme_only && strcmp(argv[2], "--theme-only") != 0) return usage();
        return osr_module_run(argv[theme_only ? 3 : 2], theme_only) ? 0 : 1;
    }
    return usage();
}
