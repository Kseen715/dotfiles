/* lib/modules.c -- the registry of Linux modules written in C.
 *
 * A rice manifest names modules and install.sh runs each one. A module is
 * either a shell script under modules (there are none left -- see DESIGN 11a)
 * or a C
 * function registered here (modules/<name>.c, the POSIX branch of it where
 * the file also has a Windows one). install.sh asks `osr module has <name>`
 * and, when the answer is yes, runs `osr module run <name>` instead of
 * sourcing the script -- so the two kinds coexist and a rice.list never has
 * to know which is which.
 *
 * Writing one in C buys what the sh tier cannot have: `osr_step` can
 * fork a FUNCTION of this program, where the shell run_step could
 * only fork a shell function. Everything a module may call is lib/module.h.
 *
 * The `session` field is the C form of a .sh module's `# session:` first line
 * (x11 / wayland / x11+wayland), which is what lets `grep -l` answer "what
 * breaks if I move this rice to Wayland" without reading every module.
 *
 *   osr module list           every C module, one per line
 *   osr module has <name>     exit 0 when this tier owns that name
 *   osr module session <name> its session marker
 *   osr module themable <name>  exit 0 when it consumes the theme
 *   osr module run <name>     install it
 *   osr module run --theme-only <name>   only its theme layer (§6a)
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

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

int osrm_alacritty(void);
int osrm_amnezia_vpn(void);
int osrm_arandr(void);
int osrm_archives(void);
int osrm_audio(void);
int osrm_avahi(void);
int osrm_benchmark(void);
int osrm_blueman(void);
int osrm_brightnessctl(void);
int osrm_btop(void);
int osrm_celluloid(void);
int osrm_cliphist(void);
int osrm_codecs(void);
int osrm_copyq(void);
int osrm_cpu_microcodes(void);
int osrm_curseforge(void);
int osrm_datagrip(void);
int osrm_discord(void);
int osrm_disks(void);
int osrm_dkms(void);
int osrm_dnscrypt(void);
int osrm_docker(void);
int osrm_dunst(void);
int osrm_easyeffects(void);
int osrm_evolution(void);
int osrm_fastfetch(void);
int osrm_fcitx5(void);
int osrm_feh(void);
int osrm_firefox(void);
int osrm_flameshot(void);
int osrm_flatpak(void);
int osrm_foot(void);
int osrm_gh(void);
int osrm_ghostty(void);
int osrm_git_base(void);
int osrm_gnome_focus(void);
int osrm_gnome_overview(void);
int osrm_go(void);
int osrm_gpaste(void);
int osrm_gpu_drivers(void);
int osrm_gtklock(void);
int osrm_gvfs(void);
int osrm_helpers(void);
int osrm_helvum(void);
int osrm_htop(void);
int osrm_hyprcursor(void);
int osrm_hypridle(void);
int osrm_hyprland(void);
int osrm_hyprlock(void);
int osrm_hyprpaper(void);
int osrm_hyprpicker(void);
int osrm_i3(void);
int osrm_i3lock(void);
int osrm_input(void);
int osrm_inxi(void);
int osrm_kate(void);
int osrm_kdeconnect(void);
int osrm_keyring(void);
int osrm_lightdm(void);
int osrm_loupe(void);
int osrm_luminance(void);
int osrm_mako(void);
int osrm_micro(void);
int osrm_mirrors(void);
int osrm_nautilus(void);
int osrm_ncdu(void);
int osrm_networkmanager(void);
int osrm_nwg_displays(void);
int osrm_obs_studio(void);
int osrm_onlyoffice(void);
int osrm_openssh(void);
int osrm_pacman_multilib(void);
int osrm_paru(void);
int osrm_picom(void);
int osrm_pipewire(void);
int osrm_polkit_agent(void);
int osrm_polybar(void);
int osrm_power(void);
int osrm_printer(void);
int osrm_proteus(void);
int osrm_pulseaudio(void);
int osrm_qbittorrent(void);
int osrm_qpwgraph(void);
int osrm_redshift(void);
int osrm_rofi(void);
int osrm_rust(void);
int osrm_sddm(void);
int osrm_serie(void);
int osrm_starship(void);
int osrm_steam(void);
int osrm_swap(void);
int osrm_swaylock(void);
int osrm_tcc(void);
int osrm_telegram(void);
int osrm_theming(void);
int osrm_thumbnails(void);
int osrm_thunar(void);
int osrm_thunderbird(void);
int osrm_ufw(void);
int osrm_viewers(void);
int osrm_vlc(void);
int osrm_vmware_init(void);
int osrm_vscode(void);
int osrm_vscode_insiders(void);
int osrm_waybar(void);
int osrm_waydroid(void);
int osrm_wayland(void);
int osrm_waylock(void);
int osrm_wezterm(void);
int osrm_wleave(void);
int osrm_wlogout(void);
int osrm_wofi(void);
int osrm_xdg(void);
int osrm_xorg(void);
int osrm_yandex_browser(void);
int osrm_yazi(void);
int osrm_zen_browser(void);
int osrm_zig(void);
int osrm_zip(void);
int osrm_zsh(void);

static const ModuleRow modules[] = {
    { "alacritty",       "x11+wayland", 1, osrm_alacritty },
    { "amnezia-vpn",     "x11+wayland", 0, osrm_amnezia_vpn },
    { "arandr",          "x11",         0, osrm_arandr },
    { "archives",        "x11+wayland", 0, osrm_archives },
    { "audio",           "x11+wayland", 0, osrm_audio },
    { "avahi",           "x11+wayland", 0, osrm_avahi },
    { "benchmark",       "x11+wayland", 0, osrm_benchmark },
    { "blueman",         "x11+wayland", 0, osrm_blueman },
    { "brightnessctl",   "x11+wayland", 0, osrm_brightnessctl },
    { "btop",            "x11+wayland", 1, osrm_btop },
    { "celluloid",       "x11+wayland", 0, osrm_celluloid },
    { "cliphist",        "wayland",     1, osrm_cliphist },
    { "codecs",          "x11+wayland", 0, osrm_codecs },
    { "copyq",           "x11",         1, osrm_copyq },
    { "cpu-microcodes",  "x11+wayland", 0, osrm_cpu_microcodes },
    { "curseforge",      "x11+wayland", 0, osrm_curseforge },
    { "datagrip",        "x11+wayland", 0, osrm_datagrip },
    { "discord",         "x11+wayland", 0, osrm_discord },
    { "disks",           "x11+wayland", 0, osrm_disks },
    { "dkms",            "x11+wayland", 0, osrm_dkms },
    { "dnscrypt",        "x11+wayland", 0, osrm_dnscrypt },
    { "docker",          "x11+wayland", 0, osrm_docker },
    { "dunst",           "x11+wayland", 1, osrm_dunst },
    { "easyeffects",     "x11+wayland", 0, osrm_easyeffects },
    { "evolution",       "x11+wayland", 1, osrm_evolution },
    { "fastfetch",       "x11+wayland", 1, osrm_fastfetch },
    { "fcitx5",          "x11+wayland", 1, osrm_fcitx5 },
    { "feh",             "x11",         0, osrm_feh },
    { "firefox",         "x11+wayland", 1, osrm_firefox },
    { "flameshot",       "x11",         0, osrm_flameshot },
    { "flatpak",         "x11+wayland", 0, osrm_flatpak },
    { "foot",            "wayland",     1, osrm_foot },
    { "gh",              "x11+wayland", 0, osrm_gh },
    { "ghostty",         "x11+wayland", 1, osrm_ghostty },
    { "git-base",        "x11+wayland", 0, osrm_git_base },
    { "gnome-focus",     "wayland",     0, osrm_gnome_focus },
    { "gnome-overview",  "x11+wayland", 0, osrm_gnome_overview },
    { "go",              "x11+wayland", 0, osrm_go },
    { "gpaste",          "x11+wayland", 0, osrm_gpaste },
    { "gpu-drivers",     "x11+wayland", 0, osrm_gpu_drivers },
    { "gtklock",         "wayland",     1, osrm_gtklock },
    { "gvfs",            "x11+wayland", 0, osrm_gvfs },
    { "helpers",         "x11+wayland", 0, osrm_helpers },
    { "helvum",          "x11+wayland", 0, osrm_helvum },
    { "htop",            "x11+wayland", 0, osrm_htop },
    { "hyprcursor",      "wayland",     0, osrm_hyprcursor },
    { "hypridle",        "wayland",     1, osrm_hypridle },
    { "hyprland",        "wayland",     1, osrm_hyprland },
    { "hyprlock",        "wayland",     1, osrm_hyprlock },
    { "hyprpaper",       "wayland",     1, osrm_hyprpaper },
    { "hyprpicker",      "wayland",     0, osrm_hyprpicker },
    { "i3",              "x11",         1, osrm_i3 },
    { "i3lock",          "x11",         1, osrm_i3lock },
    { "input",           "x11",         0, osrm_input },
    { "inxi",            "x11+wayland", 0, osrm_inxi },
    { "kate",            "x11+wayland", 1, osrm_kate },
    { "kdeconnect",      "x11+wayland", 0, osrm_kdeconnect },
    { "keyring",         "x11+wayland", 0, osrm_keyring },
    { "lightdm",         "x11",         1, osrm_lightdm },
    { "loupe",           "x11+wayland", 0, osrm_loupe },
    { "luminance",       "wayland",     0, osrm_luminance },
    { "mako",            "wayland",     1, osrm_mako },
    { "micro",           "x11+wayland", 1, osrm_micro },
    { "mirrors",         "x11+wayland", 0, osrm_mirrors },
    { "nautilus",        "x11+wayland", 0, osrm_nautilus },
    { "ncdu",            "x11+wayland", 0, osrm_ncdu },
    { "networkmanager",  "x11+wayland", 0, osrm_networkmanager },
    { "nwg-displays",    "wayland",     0, osrm_nwg_displays },
    { "obs-studio",      "x11+wayland", 0, osrm_obs_studio },
    { "onlyoffice",      "x11+wayland", 0, osrm_onlyoffice },
    { "openssh",         "x11+wayland", 0, osrm_openssh },
    { "pacman-multilib", "x11+wayland", 0, osrm_pacman_multilib },
    { "paru",            "x11+wayland", 0, osrm_paru },
    { "picom",           "x11",         1, osrm_picom },
    { "pipewire",        "x11+wayland", 0, osrm_pipewire },
    { "polkit-agent",    "x11+wayland", 0, osrm_polkit_agent },
    { "polybar",         "x11",         1, osrm_polybar },
    { "power",           "x11+wayland", 0, osrm_power },
    { "printer",         "x11+wayland", 0, osrm_printer },
    { "proteus",         "x11+wayland", 0, osrm_proteus },
    { "pulseaudio",      "x11+wayland", 0, osrm_pulseaudio },
    { "qbittorrent",     "x11+wayland", 0, osrm_qbittorrent },
    { "qpwgraph",        "x11+wayland", 0, osrm_qpwgraph },
    { "redshift",        "x11",         0, osrm_redshift },
    { "rofi",            "x11",         1, osrm_rofi },
    { "rust",            "x11+wayland", 0, osrm_rust },
    { "sddm",            "x11+wayland", 1, osrm_sddm },
    { "serie",           "x11+wayland", 1, osrm_serie },
    { "starship",        "x11+wayland", 1, osrm_starship },
    { "steam",           "x11+wayland", 0, osrm_steam },
    { "swap",            "x11+wayland", 0, osrm_swap },
    { "swaylock",        "wayland",     1, osrm_swaylock },
    { "tcc",             "x11+wayland", 0, osrm_tcc },
    { "telegram",        "x11+wayland", 1, osrm_telegram },
    { "theming",         "x11",         1, osrm_theming },
    { "thumbnails",      "x11+wayland", 0, osrm_thumbnails },
    { "thunar",          "x11+wayland", 0, osrm_thunar },
    { "thunderbird",     "x11+wayland", 1, osrm_thunderbird },
    { "ufw",             "x11+wayland", 0, osrm_ufw },
    { "viewers",         "x11+wayland", 1, osrm_viewers },
    { "vlc",             "x11+wayland", 1, osrm_vlc },
    { "vmware-init",     "x11",         0, osrm_vmware_init },
    { "vscode",          "x11+wayland", 0, osrm_vscode },
    { "vscode-insiders", "x11+wayland", 0, osrm_vscode_insiders },
    { "waybar",          "wayland",     1, osrm_waybar },
    { "waydroid",        "wayland",     0, osrm_waydroid },
    { "wayland",         "wayland",     0, osrm_wayland },
    { "waylock",         "wayland",     1, osrm_waylock },
    { "wezterm",         "x11+wayland", 1, osrm_wezterm },
    { "wleave",          "wayland",     1, osrm_wleave },
    { "wlogout",         "wayland",     1, osrm_wlogout },
    { "wofi",            "wayland",     1, osrm_wofi },
    { "xdg",             "x11+wayland", 0, osrm_xdg },
    { "xorg",            "x11",         1, osrm_xorg },
    { "yandex-browser",  "x11+wayland", 0, osrm_yandex_browser },
    { "yazi",            "x11+wayland", 1, osrm_yazi },
    { "zen-browser",     "x11+wayland", 1, osrm_zen_browser },
    { "zig",             "x11+wayland", 0, osrm_zig },
    { "zip",             "x11+wayland", 0, osrm_zip },
    { "zsh",             "x11+wayland", 1, osrm_zsh }
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
    return m->run();
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
    str_addz(&path, env_str("OSR_ROOT", "."));
    str_addz(&path, "/modules/");
    str_addz(&path, name);
    str_addz(&path, ".sh");
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
        str_addz(out, modules[i].name);
        str_addc(out, '\n');
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
