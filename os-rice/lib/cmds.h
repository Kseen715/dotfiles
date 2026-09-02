/* lib/cmds.h -- the POSIX harness's commands.
 *
 * One binary (build/osr) holds all of them, the same way the Windows core
 * links install.c with its lib units; osr.c dispatches on argv[1] and each
 * command lives in its own translation unit, named after the lib/<x>.sh it
 * replaced. Every entry takes the argument vector AFTER the command word and
 * returns the exit status.
 *
 * C89 + POSIX.
 */
#ifndef OSR_CMDS_H
#define OSR_CMDS_H

int osr_ui_main(int argc, char **argv);       /* lib/ui.sh */
int osr_log_main(int argc, char **argv);      /* lib/log.sh */

/* osr_log_step -- an [INFO] line carrying the "[03/12] " step counter, for a
 * caller inside this process (the runner, per module). osr_step_prefix is that
 * counter alone, for a caller composing its own line; buf must hold 32 bytes. */
void osr_log_step(const char *msg);
const char *osr_step_prefix(char *buf, size_t buf_sz);
int osr_state_main(int argc, char **argv);    /* lib/state.sh */

/* osr_state_get / osr_state_set -- the state file from inside this process,
 * where the shell tier spent a fork on `osr state get`. get yields the value
 * without a trailing newline, the way `$( )` handed it to sh. */
void osr_state_get(Str *out, const char *key);
int osr_state_set(const char *key, const char *value);
int osr_user_main(int argc, char **argv);     /* lib/user.sh */

/* The login-shell half of lib/user.sh, for a caller inside this process. They
 * write, which is why they were the last of that lib to stay in sh: as_root
 * was a shell function, and osr_run_root is the same escalation without one.
 * osr_set_login_shell returns 1 only when the account really ends up with that
 * shell -- chsh, usermod and a direct /etc/passwd rewrite, each verified. */
int osr_user_shell_is(const char *user, const char *shell);
int osr_register_shell(const char *shell);
int osr_set_login_shell(const char *user, const char *shell);

/* osr_resolve_user -- which account is being riced and where it lives, into
 * this process's environment as OSR_USER/OSR_HOME for the runner and every
 * child it forks (install.sh's `export`). Order: --user > $SUDO_USER >
 * $USER > whoever we are (§8). */
void osr_resolve_user(const char *explicit_user);
int osr_theme_main(int argc, char **argv);    /* lib/theme.sh */

/* osr_theme_meta -- a theme's single-valued `key: value` field, appended to
 * out and left empty when the theme does not define it (osr_theme_meta). */
void osr_theme_meta(Str *out, const char *theme, const char *key);

/* The shell-callable half of lib/theme.sh, in process. The shim existed to set
 * OSR_THEME/OSR_THEME_DIR for everything downstream; a C caller sets them with
 * setenv and every child it forks inherits them, which is what `export` bought.
 *
 * osr_resolve_theme is fatal on a name that is not a theme -- the user typed
 * that one, and guessing at it paints the wrong desktop silently. With no name
 * it asks (the numbered picker on /dev/tty), and with no terminal to ask on it
 * takes $OSR_DEFAULT_THEME. */
int osr_theme_exists(const char *name);
void osr_theme_list(Str *out);
void osr_theme_menu(Str *out);
void osr_theme_configs(Str *out, const char *name);
void osr_rice_default_theme(Str *out, const char *rice);
void osr_resolve_theme(const char *want);
void osr_unset_theme(void);
int osr_apply_theme_configs(void);

/* osr_theme_read_lines -- a manifest's directive lines, as a `while IFS= read`
 * loop over `osr theme lines` would have seen them (lib/theme.c). */
void osr_theme_read_lines(Str *out, const char *path);
int osr_detect_main(int argc, char **argv);   /* lib/detect.sh */

/* osr_detect_export -- osr_detect, straight into this process's environment,
 * for the runner: the facts set with setenv so every child it forks inherits
 * them, which is what the sh `export` bought. `what` is "all" or one probe's
 * name ("ram", the one the runner re-runs after warming a sudo ticket). */
void osr_detect_export(const char *what);

/* osr_gpu_chip -- the chip codename of the first GPU of that vendor, out of
 * the OSR_GPU_DEVICES osr_detect exported. 0 when this box has no such GPU,
 * which is different from one whose codename lspci could not name. */
int osr_gpu_chip(Str *out, const char *vendor);
int osr_install_main(int argc, char **argv);  /* install.sh */
int osr_module_main(int argc, char **argv);   /* the Linux C modules */
int osr_pkg_main(int argc, char **argv);      /* lib/pkg.sh */

#ifdef _WIN32
/* osr_reg_read_str -- one REG_SZ registry value into a bounded buffer, 1 when
 * it was there and non-empty. lib/pkg.c owns it because that is where the
 * registry reading started (re-reading the environment after an install);
 * lib/detect.c reads the same hive for the version facets, and a second copy
 * of twelve lines of RegQueryValueEx is not worth having. `root` is an HKEY,
 * passed as void * so a caller need not include windows.h to name one. */
int osr_reg_read_str(void *root, const char *subkey, const char *value,
                     char *out, unsigned long out_sz);
#endif
int osr_net_main(int argc, char **argv);      /* lib/net.sh */
int osr_build_main(int argc, char **argv);    /* lib/build.sh */
int osr_config_main(int argc, char **argv);   /* lib/config.sh */
int osr_git_main(int argc, char **argv);      /* lib/git.sh */
int osr_service_main(int argc, char **argv);  /* lib/service.sh */
int osr_preflight_main(int argc, char **argv);/* lib/preflight.sh */
int osr_fonts_main(int argc, char **argv);    /* lib/fonts.sh */
int osr_gnome_main(int argc, char **argv);    /* lib/gnome.sh */
int osr_migrate_main(int argc, char **argv);  /* lib/migrate.sh */
int osr_apply_main(int argc, char **argv);    /* lib/apply.sh */
int osr_reload_main(int argc, char **argv);   /* lib/reload.sh */
/* osr_benchmark_main -- measure the CPU: throughput, power, thermals, clocks.
 * Standalone, and the source of the numbers the undervolt perf gate compares. */
int osr_benchmark_main(int argc, char **argv);

/* osr_undervolt_main -- CPU voltage offsets. No .sh predecessor: this one is
 * new, and is in C because it pokes MSRs and sysfs byte-blocks and has to
 * survive the machine dying halfway through a write. */
int osr_undervolt_main(int argc, char **argv);

/* osr_module_names -- the C modules' names, one per line, for the listing
 * install.sh prints (which merges them with the shell ones). */
void osr_module_names(Str *out);

/* osr_module_themable -- does this module consume the resolved theme? Drives
 * whether install.sh has any reason to ask which theme to use. */
int osr_module_themable(const char *name);

/* osr_module_has / osr_module_run -- the C module tier, called in process
 * rather than through `osr module ...`. theme_only is the §6a pass. Run
 * returns 1 for success; the caller decides whether a failure ends the run. */
int osr_module_has(const char *name);
int osr_module_run(const char *name, int theme_only);
/* osr_wallpaper_main -- wallpaper.sh: set or query the current theme's
 * wallpaper. Its own command rather than a mode of `osr install`, the same
 * separation wallpaper.sh has from install.sh: this is not an install. */
int osr_wallpaper_main(int argc, char **argv);/* wallpaper.sh */
int osr_testrun_main(int argc, char **argv);  /* test/run.sh */

#endif /* OSR_CMDS_H */
