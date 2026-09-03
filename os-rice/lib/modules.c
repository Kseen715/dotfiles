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
#define _POSIX_C_SOURCE 200809L
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

/* Modules both systems have. */
int osrm_fastfetch(void);
int osrm_starship(void);
int osrm_wezterm(void);

#ifdef _WIN32
/* Windows-only: the app modules whose program is Windows-only, and the
 * win- group, which is not app modules at all -- one OS-level pass each
 * over the machine itself. See modules/WINDOWS.md. */
int osrm_pwsh(void);
int osrm_oh_my_posh(void);
int osrm_win_tweaks(void);
int osrm_win_update(void);
int osrm_win_debloat(void);
int osrm_win_winutil(void);
#else
/* POSIX-only: every one of these installs a program that assumes an X11 or
 * Wayland desktop, a systemd/openrc unit, or a distro package manager. */
int osrm_alacritty(void);
int osrm_amnezia_vpn(void);
int osrm_arandr(void);
int osrm_archives(void);
int osrm_arocc(void);
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
int osrm_cproc(void);
int osrm_cpu_microcodes(void);
int osrm_cuik(void);
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
int osrm_gnome_panel(void);
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
int osrm_lacc(void);
int osrm_lcc(void);
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
int osrm_shecc(void);
int osrm_smallerc(void);
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
int osrm_wleave(void);
int osrm_wlogout(void);
int osrm_wofi(void);
int osrm_xcc(void);
int osrm_xdg(void);
int osrm_xorg(void);
int osrm_yandex_browser(void);
int osrm_yazi(void);
int osrm_zen_browser(void);
int osrm_zig(void);
int osrm_zip(void);
int osrm_zsh(void);
#endif

static const ModuleRow modules[] = {
#ifdef _WIN32
    { "fastfetch",       "windows",     1, MODULE_RUN(osrm_fastfetch) },
    { "oh-my-posh",      "windows",     1, MODULE_RUN(osrm_oh_my_posh) },
    { "pwsh",            "windows",     0, MODULE_RUN(osrm_pwsh) },
    { "starship",        "windows",     1, MODULE_RUN(osrm_starship) },
    { "wezterm",         "windows",     1, MODULE_RUN(osrm_wezterm) },
    { "win-debloat",     "windows",     0, MODULE_RUN(osrm_win_debloat) },
    { "win-tweaks",      "windows",     0, MODULE_RUN(osrm_win_tweaks) },
    { "win-update",      "windows",     0, MODULE_RUN(osrm_win_update) },
    { "win-winutil",     "windows",     0, MODULE_RUN(osrm_win_winutil) }
#else
    { "alacritty",       "x11+wayland", 1, MODULE_RUN(osrm_alacritty) },
    { "amnezia-vpn",     "x11+wayland", 0, MODULE_RUN(osrm_amnezia_vpn) },
    { "arandr",          "x11",         0, MODULE_RUN(osrm_arandr) },
    { "archives",        "x11+wayland", 0, MODULE_RUN(osrm_archives) },
    { "arocc",           "x11+wayland", 0, MODULE_RUN(osrm_arocc) },
    { "audio",           "x11+wayland", 0, MODULE_RUN(osrm_audio) },
    { "avahi",           "x11+wayland", 0, MODULE_RUN(osrm_avahi) },
    { "benchmark",       "x11+wayland", 0, MODULE_RUN(osrm_benchmark) },
    { "blueman",         "x11+wayland", 0, MODULE_RUN(osrm_blueman) },
    { "brightnessctl",   "x11+wayland", 0, MODULE_RUN(osrm_brightnessctl) },
    { "btop",            "x11+wayland", 1, MODULE_RUN(osrm_btop) },
    { "celluloid",       "x11+wayland", 0, MODULE_RUN(osrm_celluloid) },
    { "cliphist",        "wayland",     1, MODULE_RUN(osrm_cliphist) },
    { "codecs",          "x11+wayland", 0, MODULE_RUN(osrm_codecs) },
    { "copyq",           "x11",         1, MODULE_RUN(osrm_copyq) },
    { "cproc",           "x11+wayland", 0, MODULE_RUN(osrm_cproc) },
    { "cpu-microcodes",  "x11+wayland", 0, MODULE_RUN(osrm_cpu_microcodes) },
    { "cuik",            "x11+wayland", 0, MODULE_RUN(osrm_cuik) },
    { "curseforge",      "x11+wayland", 0, MODULE_RUN(osrm_curseforge) },
    { "datagrip",        "x11+wayland", 0, MODULE_RUN(osrm_datagrip) },
    { "discord",         "x11+wayland", 0, MODULE_RUN(osrm_discord) },
    { "disks",           "x11+wayland", 0, MODULE_RUN(osrm_disks) },
    { "dkms",            "x11+wayland", 0, MODULE_RUN(osrm_dkms) },
    { "dnscrypt",        "x11+wayland", 0, MODULE_RUN(osrm_dnscrypt) },
    { "docker",          "x11+wayland", 0, MODULE_RUN(osrm_docker) },
    { "dunst",           "x11+wayland", 1, MODULE_RUN(osrm_dunst) },
    { "easyeffects",     "x11+wayland", 0, MODULE_RUN(osrm_easyeffects) },
    { "evolution",       "x11+wayland", 1, MODULE_RUN(osrm_evolution) },
    { "fastfetch",       "x11+wayland", 1, MODULE_RUN(osrm_fastfetch) },
    { "fcitx5",          "x11+wayland", 1, MODULE_RUN(osrm_fcitx5) },
    { "feh",             "x11",         0, MODULE_RUN(osrm_feh) },
    { "firefox",         "x11+wayland", 1, MODULE_RUN(osrm_firefox) },
    { "flameshot",       "x11",         0, MODULE_RUN(osrm_flameshot) },
    { "flatpak",         "x11+wayland", 0, MODULE_RUN(osrm_flatpak) },
    { "foot",            "wayland",     1, MODULE_RUN(osrm_foot) },
    { "gh",              "x11+wayland", 0, MODULE_RUN(osrm_gh) },
    { "ghostty",         "x11+wayland", 1, MODULE_RUN(osrm_ghostty) },
    { "git-base",        "x11+wayland", 0, MODULE_RUN(osrm_git_base) },
    { "gnome-focus",     "wayland",     0, MODULE_RUN(osrm_gnome_focus) },
    { "gnome-overview",  "x11+wayland", 0, MODULE_RUN(osrm_gnome_overview) },
    { "gnome-panel",     "x11+wayland", 1, MODULE_RUN(osrm_gnome_panel) },
    { "go",              "x11+wayland", 0, MODULE_RUN(osrm_go) },
    { "gpaste",          "x11+wayland", 0, MODULE_RUN(osrm_gpaste) },
    { "gpu-drivers",     "x11+wayland", 0, MODULE_RUN(osrm_gpu_drivers) },
    { "gtklock",         "wayland",     1, MODULE_RUN(osrm_gtklock) },
    { "gvfs",            "x11+wayland", 0, MODULE_RUN(osrm_gvfs) },
    { "helpers",         "x11+wayland", 0, MODULE_RUN(osrm_helpers) },
    { "helvum",          "x11+wayland", 0, MODULE_RUN(osrm_helvum) },
    { "htop",            "x11+wayland", 0, MODULE_RUN(osrm_htop) },
    { "hyprcursor",      "wayland",     0, MODULE_RUN(osrm_hyprcursor) },
    { "hypridle",        "wayland",     1, MODULE_RUN(osrm_hypridle) },
    { "hyprland",        "wayland",     1, MODULE_RUN(osrm_hyprland) },
    { "hyprlock",        "wayland",     1, MODULE_RUN(osrm_hyprlock) },
    { "hyprpaper",       "wayland",     1, MODULE_RUN(osrm_hyprpaper) },
    { "hyprpicker",      "wayland",     0, MODULE_RUN(osrm_hyprpicker) },
    { "i3",              "x11",         1, MODULE_RUN(osrm_i3) },
    { "i3lock",          "x11",         1, MODULE_RUN(osrm_i3lock) },
    { "input",           "x11",         0, MODULE_RUN(osrm_input) },
    { "inxi",            "x11+wayland", 0, MODULE_RUN(osrm_inxi) },
    { "kate",            "x11+wayland", 1, MODULE_RUN(osrm_kate) },
    { "kdeconnect",      "x11+wayland", 0, MODULE_RUN(osrm_kdeconnect) },
    { "keyring",         "x11+wayland", 0, MODULE_RUN(osrm_keyring) },
    { "lacc",            "x11+wayland", 0, MODULE_RUN(osrm_lacc) },
    { "lcc",             "x11+wayland", 0, MODULE_RUN(osrm_lcc) },
    { "lightdm",         "x11",         1, MODULE_RUN(osrm_lightdm) },
    { "loupe",           "x11+wayland", 0, MODULE_RUN(osrm_loupe) },
    { "luminance",       "wayland",     0, MODULE_RUN(osrm_luminance) },
    { "mako",            "wayland",     1, MODULE_RUN(osrm_mako) },
    { "micro",           "x11+wayland", 1, MODULE_RUN(osrm_micro) },
    { "mirrors",         "x11+wayland", 0, MODULE_RUN(osrm_mirrors) },
    { "nautilus",        "x11+wayland", 0, MODULE_RUN(osrm_nautilus) },
    { "ncdu",            "x11+wayland", 0, MODULE_RUN(osrm_ncdu) },
    { "networkmanager",  "x11+wayland", 0, MODULE_RUN(osrm_networkmanager) },
    { "nwg-displays",    "wayland",     0, MODULE_RUN(osrm_nwg_displays) },
    { "obs-studio",      "x11+wayland", 0, MODULE_RUN(osrm_obs_studio) },
    { "onlyoffice",      "x11+wayland", 0, MODULE_RUN(osrm_onlyoffice) },
    { "openssh",         "x11+wayland", 0, MODULE_RUN(osrm_openssh) },
    { "pacman-multilib", "x11+wayland", 0, MODULE_RUN(osrm_pacman_multilib) },
    { "paru",            "x11+wayland", 0, MODULE_RUN(osrm_paru) },
    { "picom",           "x11",         1, MODULE_RUN(osrm_picom) },
    { "pipewire",        "x11+wayland", 0, MODULE_RUN(osrm_pipewire) },
    { "polkit-agent",    "x11+wayland", 0, MODULE_RUN(osrm_polkit_agent) },
    { "polybar",         "x11",         1, MODULE_RUN(osrm_polybar) },
    { "power",           "x11+wayland", 0, MODULE_RUN(osrm_power) },
    { "printer",         "x11+wayland", 0, MODULE_RUN(osrm_printer) },
    { "proteus",         "x11+wayland", 0, MODULE_RUN(osrm_proteus) },
    { "pulseaudio",      "x11+wayland", 0, MODULE_RUN(osrm_pulseaudio) },
    { "qbittorrent",     "x11+wayland", 0, MODULE_RUN(osrm_qbittorrent) },
    { "qpwgraph",        "x11+wayland", 0, MODULE_RUN(osrm_qpwgraph) },
    { "redshift",        "x11",         0, MODULE_RUN(osrm_redshift) },
    { "rofi",            "x11",         1, MODULE_RUN(osrm_rofi) },
    { "rust",            "x11+wayland", 0, MODULE_RUN(osrm_rust) },
    { "sddm",            "x11+wayland", 1, MODULE_RUN(osrm_sddm) },
    { "serie",           "x11+wayland", 1, MODULE_RUN(osrm_serie) },
    { "shecc",           "x11+wayland", 0, MODULE_RUN(osrm_shecc) },
    { "smallerc",        "x11+wayland", 0, MODULE_RUN(osrm_smallerc) },
    { "starship",        "x11+wayland", 1, MODULE_RUN(osrm_starship) },
    { "steam",           "x11+wayland", 0, MODULE_RUN(osrm_steam) },
    { "swap",            "x11+wayland", 0, MODULE_RUN(osrm_swap) },
    { "swaylock",        "wayland",     1, MODULE_RUN(osrm_swaylock) },
    { "tcc",             "x11+wayland", 0, MODULE_RUN(osrm_tcc) },
    { "telegram",        "x11+wayland", 1, MODULE_RUN(osrm_telegram) },
    { "theming",         "x11",         1, MODULE_RUN(osrm_theming) },
    { "thumbnails",      "x11+wayland", 0, MODULE_RUN(osrm_thumbnails) },
    { "thunar",          "x11+wayland", 0, MODULE_RUN(osrm_thunar) },
    { "thunderbird",     "x11+wayland", 1, MODULE_RUN(osrm_thunderbird) },
    { "ufw",             "x11+wayland", 0, MODULE_RUN(osrm_ufw) },
    { "viewers",         "x11+wayland", 1, MODULE_RUN(osrm_viewers) },
    { "vlc",             "x11+wayland", 1, MODULE_RUN(osrm_vlc) },
    { "vmware-init",     "x11",         0, MODULE_RUN(osrm_vmware_init) },
    { "vscode",          "x11+wayland", 0, MODULE_RUN(osrm_vscode) },
    { "vscode-insiders", "x11+wayland", 0, MODULE_RUN(osrm_vscode_insiders) },
    { "waybar",          "wayland",     1, MODULE_RUN(osrm_waybar) },
    { "waydroid",        "wayland",     0, MODULE_RUN(osrm_waydroid) },
    { "wayland",         "wayland",     0, MODULE_RUN(osrm_wayland) },
    { "waylock",         "wayland",     1, MODULE_RUN(osrm_waylock) },
    { "wezterm",         "x11+wayland", 1, MODULE_RUN(osrm_wezterm) },
    { "wleave",          "wayland",     1, MODULE_RUN(osrm_wleave) },
    { "wlogout",         "wayland",     1, MODULE_RUN(osrm_wlogout) },
    { "wofi",            "wayland",     1, MODULE_RUN(osrm_wofi) },
    { "xcc",             "x11+wayland", 0, MODULE_RUN(osrm_xcc) },
    { "xdg",             "x11+wayland", 0, MODULE_RUN(osrm_xdg) },
    { "xorg",            "x11",         1, MODULE_RUN(osrm_xorg) },
    { "yandex-browser",  "x11+wayland", 0, MODULE_RUN(osrm_yandex_browser) },
    { "yazi",            "x11+wayland", 1, MODULE_RUN(osrm_yazi) },
    { "zen-browser",     "x11+wayland", 1, MODULE_RUN(osrm_zen_browser) },
    { "zig",             "x11+wayland", 0, MODULE_RUN(osrm_zig) },
    { "zip",             "x11+wayland", 0, MODULE_RUN(osrm_zip) },
    { "zsh",             "x11+wayland", 1, MODULE_RUN(osrm_zsh) }
#endif
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
