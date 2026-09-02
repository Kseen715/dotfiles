/* lib/module.h -- the API a Linux module written in C calls.
 *
 * A module is one translation unit at modules/<name>.c, registered in
 * lib/modules.c, that installs one thing: package, config, service. That file
 * is the module on every OS, not just this one: a module both systems can
 * have holds a Windows branch too (modules/fastfetch.c), behind #ifdef
 * _WIN32, and only the branch below the #else is what this header describes.
 * This header is everything a POSIX module is allowed to assume. Nothing here needs a shell:
 * the point of writing a module in C is that `osr_run_step` can fork a real
 * function or a real command, where the sh `run_step` could only fork a shell
 * function -- which is the single reason lib/ui.sh still exists.
 *
 * A module is called with the facts already detected and exported (OSR_PKG,
 * OSR_DISTRO, OSR_USER, OSR_HOME, OSR_THEME, ...), because install.sh runs
 * `osr module run <name>` after osr_detect and osr_resolve_user. Read them
 * through the accessors below rather than getenv, so a future in-process
 * caller can supply them without an environment.
 *
 * Return 1 for success, 0 for failure. A failing module is reported and the
 * run continues -- one broken module must not abort a whole rice install,
 * same contract as modules.h has on the Windows side.
 *
 * C89 + POSIX.
 */
#ifndef OSR_MODULE_H
#define OSR_MODULE_H

#include "common.h"

/* --- what the module was given ------------------------------------------- */
const char *osr_mod_root(void);      /* the os-rice/ directory */
const char *osr_mod_dotfiles(void);  /* the dotfiles checkout (its parent) */
const char *osr_mod_user(void);      /* the account being riced (§8) */
const char *osr_mod_home(void);      /* that account's home */
const char *osr_mod_theme(void);     /* the resolved theme name, "" if none */
const char *osr_mod_theme_dir(void); /* themes/<name>, "" if none */
const char *osr_mod_pkg(void);       /* apt | dnf | pacman | apk | xbps | portage */
const char *osr_mod_distro(void);    /* the os-release ID */
const char *osr_mod_init(void);      /* systemd | openrc | runit | sysvinit */

/* --- saying things --------------------------------------------------------
 * The same five lines as lib/log.sh, printf-style: osr_infof, osr_debugf,
 * osr_warnf, osr_successf and osr_die (which prints and exits 1 -- lib/log.sh's
 * error(), the one fatal path). They are declared in common.h, included above,
 * because BOTH cores print through them; a module needs no other include to
 * say something.
 * ------------------------------------------------------------------------- */

/* --- theme-only mode ------------------------------------------------------
 * §6a. `osr theme <name>` runs the SAME modules a rice install runs, with every
 * mutating verb neutralized first; what survives is the file copying, which is
 * what a theme IS. In the shell tier that is osr_apply_stub_mutators, which
 * redefines the functions of the mutating libs in the shell the modules are
 * then sourced into. A C module is not sourced into anything, so the same
 * decision is made here instead: the mutating entry points of lib/{pkg,build,
 * fetch,git,service,nerdfont}.c check this flag and become logging no-ops.
 *
 * The two lists have to say the same thing. The sh one is DERIVED (every
 * function the mutating libs define, minus a read-only allowlist) and this one
 * is enumerated, because C has no way to ask a translation unit what it
 * defines -- so lib/apply.c's verb list stays the specification, and
 * test/unit/theme_apply.sh is what holds them together.
 *
 * Queries stay live on both sides: pkgmap resolution, "is this installed",
 * service name lookup, a version probe. A module branches on those, and a
 * stubbed query answers "" for everything, which is a different module.
 */
int osr_theme_only(void);
void osr_set_theme_only(int on);

/* osr_theme_only_skip -- log the verb that is being skipped and return 1, the
 * body every neutralized entry point shares. */
int osr_theme_only_skip(const char *verb);

/* --- running things ------------------------------------------------------- */
/* osr_run_step -- run argv under the live step window (dimmed tail of its
 * output, spinner, collapsing to one [ok]/[!!] line), or plain streamed lines
 * off a TTY / under --verbose. Exactly what `run_step "<desc>" cmd...` did,
 * FATALITY INCLUDED: the sh run_step ended the run with `error "<desc>
 * failed"`, so a failing step here prints the same line and exits. That is the
 * "single fatal path" the whole harness is built on - a module must not limp
 * on after a mutation half-applied - and it is why the return value is only
 * ever 1.
 */
int osr_run_step(const char *desc, char *const argv[]);

/* osr_step -- the same live window around a FUNCTION of this program: fn runs
 * in a forked child whose output is captured, so a step can be "install these
 * packages" rather than "run this one command". This is the thing the shell
 * tier could not do for a helper process -- `run_step pkg_install foo` worked
 * only because pkg_install was a shell function -- and it is the reason a
 * module written in C needs no shell at all. fn returns 1 for success.
 */
int osr_step(const char *desc, int (*fn)(void *ctx), void *ctx);

/* osr_step_try -- the same step, in a child, so a failure is reported and the
 * run continues. The C form of `( run_step "..." <verb> )`: the subshell was
 * what kept run_step's error() from ending the install, and it exists for the
 * one shape that needs it -- an OPTIONAL package only some distros carry. */
int osr_step_try(const char *desc, int (*fn)(void *ctx), void *ctx);

/* osr_pkg_install_step -- the shape almost every module opens with:
 * `run_step "<desc>" pkg_install <names...>`. */
int osr_pkg_install_step(const char *desc, const char *const names[]);
/* osr_pkg_install_step_try -- the same, non-fatal: for a package the rice would
 * like but can live without. */
int osr_pkg_install_step_try(const char *desc, const char *const names[]);

/* osr_run / osr_run_root / osr_run_user -- run argv now, output straight
 * through, returning its exit status. The last two are as_root/as_user: they
 * prepend sudo only when the current identity is not the wanted one. */
int osr_run(char *const argv[]);
int osr_run_root(char *const argv[]);
int osr_run_user(char *const argv[]);

/* osr_run_root_quiet -- as_root with stdout and stderr discarded, for the
 * best-effort probes lib/pkg.sh spells `as_root <cmd> >/dev/null 2>&1 || :`. */
int osr_run_root_quiet(char *const argv[]);

/* osr_run_user_quiet -- as_user with stdout and stderr discarded, for a
 * best-effort action whose failure is reported by us, not by it. */
int osr_run_user_quiet(char *const argv[]);

/* osr_run_user_in -- as_user with stdin taken from in_fd: the receiving half
 * of a `<fetch> | as_user sh -s -- ...` pipeline. */
int osr_run_user_in(char *const argv[], int in_fd);

/* osr_run_user_quiet_in -- the same, with stdout on /dev/null: an append made
 * through `tee -a` must not echo what it wrote. */
int osr_run_user_quiet_in(char *const argv[], int in_fd);

/* osr_run_root_in -- as_root with stdin taken from in_fd: the privileged half
 * of a `<fetch> | as_root bash` pipeline, which is how an upstream installer
 * script that must run as root is fed (lib/build.sh's provide_ghostty_deb). */
int osr_run_root_in(char *const argv[], int in_fd);

/* osr_run_root_quiet_in -- the same with stdout on /dev/null: a file written
 * through `as_root tee <path> >/dev/null` must not echo what it wrote. */
int osr_run_root_quiet_in(char *const argv[], int in_fd);

/* osr_run_step_root -- run_step around an as_root command, the
 * `run_step "..." as_root <cmd>` every privileged step used. */
int osr_run_step_root(const char *desc, char *const argv[]);

/* osr_run_step_user -- run_step around an as_user command. */
int osr_run_step_user(const char *desc, char *const argv[]);

/* osr_run_capture -- run argv and collect its stdout (stderr discarded), for
 * the probes modules make (`id -nG`, ...). Returns 1 when it exited 0. */
int osr_run_capture(char *const argv[], Str *out);

/* osr_run_capture_err -- the same with stderr folded in, for a tool whose
 * report goes to stderr (wget --spider -S prints the headers there). */
int osr_run_capture_err(char *const argv[], Str *out);

/* osr_run_root_capture -- `as_root <cmd> 2>&1`: a privileged probe whose
 * diagnostics ARE the answer (xbps's conflict report), so stderr is folded
 * into the captured text instead of discarded. */
int osr_run_root_capture(char *const argv[], Str *out);

/* osr_run_user_capture -- `as_user <cmd> 2>/dev/null`: a probe that has to be
 * made as the riced account, stderr discarded. */
int osr_run_user_capture(char *const argv[], Str *out);

/* osr_have_cmd -- `command -v <name>`. */
int osr_have_cmd(const char *name);

/* osr_setcap -- put a file capability set on an installed program, e.g.
 * "cap_perfmon+ep" so it can open a hardware PMU without being root or without
 * the box loosening kernel.perf_event_paranoid for every process on it.
 * Best-effort: a program that is not installed, and a filesystem that carries
 * no xattrs (overlayfs in a container), are both a 0 and neither is fatal.
 *
 * File capabilities do not survive the package being upgraded (dpkg and pacman
 * both replace the file), so a successful grant also installs the apt or pacman
 * hook that reapplies it -- rather than holding the package at a version, which
 * would trade the upgrade away for the capability and overrule a piece of state
 * that is the user's (lib/pkg.c, G2). On a manager with no hook mechanism the
 * loss is warned about and a module rerun is the fix. */
int osr_setcap(const char *caps, const char *cmd);

/* --- packages ------------------------------------------------------------- */
/* osr_pkg_install -- the native half of lib/pkg.sh: resolve each logical name
 * through lib/pkgmap/, skip what is already installed, refresh the index once
 * per run, then one install command for the rest. names is NULL-terminated.
 *
 * Provider-tagged rows are handled too, in a second pass that keeps manifest
 * order: `script:` (a piped installer), `cargo:` (a crate as OSR_USER),
 * `aur:` (paru/yay) and `source:` (a builder in lib/build.c -- or, for a
 * builder that has not been ported yet, that one row back through lib/pkg.sh).
 * The pass order is not cosmetic: the native batch carries the downloaders and
 * toolchains a provider row may need, so it cannot run second.
 */
int osr_pkg_install(const char *const names[]);
int osr_pkg_installed(const char *name);

/* osr_pkg_need -- install one logical name, treating it as already present
 * when `test_command` resolves on PATH.
 *
 * The difference from osr_pkg_install is only which question decides "already
 * done": the package NAME for the ordinary path, a COMMAND for this one. That
 * matters where the two differ -- a builder that needs `cargo` asks for the
 * package `rustup`, because rustup is the installer and cargo is what the
 * build actually needs -- and it is why the builders installing their own
 * dependencies call this rather than the plain form. test_command may be NULL,
 * which means "the name itself". */
int osr_pkg_need(const char *name, const char *test_command);

/* osr_pkg_native_installed -- the native package database's answer for a REAL
 * package name, with no pkgmap resolution in front of it (_native_installed).
 * A builder uses it to report a distro package that shadows the tree it
 * installs -- reported, never removed (§5). */
int osr_pkg_native_installed(const char *pkg);

/* osr_pkg_cargo -- the cargo: provider on its own: install <crate> as OSR_USER
 * into ~/.cargo/bin (cargo-binstall first where it exists, then a source
 * build), behind the same `as_user test -x` probe. Exposed because a source:
 * builder may want it as its FALLBACK when no prebuilt release asset exists for
 * this target -- lib/build.sh's provide_yazi_bin ends in exactly that call.
 * Returns 1 on success. */
int osr_pkg_cargo(const char *name, const char *crate);

/* osr_pkg_remove -- pkg_remove: resolve, drop what is not installed, then one
 * remove command. Providers own their own removal, so a non-native row is
 * warned about and skipped. */
int osr_pkg_remove(const char *const names[]);
/* osr_pkg_remove_step -- `run_step "<desc>" pkg_remove <names...>`. */
int osr_pkg_remove_step(const char *desc, const char *const names[]);

/* osr_pkg_refresh -- pkg_refresh: bring the package index up to date, once per
 * run. A module calls it after CHANGING what the index covers (enabling a
 * repository); every install path already refreshes on its own. */
void osr_pkg_refresh(void);

/* osr_pkg_aur_helper -- "paru", "yay", or "" (_osr_aur_helper). Resolved at
 * call time, not during detection: paru is often BUILT mid-run. A module needs
 * it only to pass flags no `aur:` row can carry (curseforge's --skipchecksums). */
const char *osr_pkg_aur_helper(void);
/* osr_pkgmap_resolve -- the logical name -> real package name(s) mapping by
 * itself (lib/pkgmap/<manager>.map then any.map, facet-qualified keys first). */
void osr_pkgmap_resolve(Str *out, const char *name);

/* osr_apt_prune_bootstrap_lists -- drop the bootstrap source lists os-rice wrote
 * for a repo whose vendor package has since written its own. Two lists for one
 * URI with different signed-by keys is what apt 3.0 refuses to parse at all,
 * taking every later apt call on the box down with it. */
void osr_apt_prune_bootstrap_lists(void);

/* --- services ------------------------------------------------------------- */
/* osr_service_enable -- enable + start a service under whatever init this box
 * runs, resolving the logical name through lib/servicemap/ first. */
int osr_service_enable(const char *name);

/* --- files ---------------------------------------------------------------- */
/* All of these write as OSR_USER (through sudo -u when the installer is not
 * that account), the same rule lib/user.sh's as_user gave the sh modules. */

/* osr_install_file -- backup_copy: back dst up to dst.bak once, then copy,
 * skipping the write when the contents already match (§2 rerun-safe). */
int osr_install_file(const char *src, const char *dst);
/* osr_ensure_line -- append line to file if it is not already in there. */
int osr_ensure_line(const char *file, const char *line);
/* osr_mkdir_p -- as_user mkdir -p. */
int osr_mkdir_p(const char *dir);
/* osr_mkdir_p_all -- the same for several directories in ONE command, which is
 * what `as_user mkdir -p "$a" "$b"` was: not an optimisation, a module that
 * forks twice where the sh one forked once is a different command log. */
int osr_mkdir_p_all(const char *const dirs[]);

/* osr_write_root / osr_append_root -- `as_root tee [-a] <path> >/dev/null`: a
 * file this program owns at a path only root can write (a .desktop entry, an
 * apt source list, a PAM stack line). Content is written verbatim - include the
 * trailing newline. Distinct from osr_seed_file_root, which writes ONCE and
 * never again: these two rewrite, which is right only where the file is ours. */
int osr_write_root(const char *path, const char *text);
int osr_append_root(const char *path, const char *text);

/* osr_write_user / osr_append_user -- the same as OSR_USER, for a file under
 * that account's own $HOME which this program owns and rewrites. Pick by who
 * should OWN the result, not by who is running: the pairing rule is the one
 * osr_seed_file / osr_seed_file_root document. */
int osr_write_user(const char *path, const char *text);
int osr_append_user(const char *path, const char *text);

/* osr_install_layer -- install_layer: one owned config file into place
 * (backup once, then copy, skipping an identical write). */
int osr_install_layer(const char *src, const char *dst);

/* osr_seed_file / osr_seed_file_root -- the `[ -f x ] || tee x <<'EOF'` shape
 * that nearly every .sh module uses for a file it must NOT own: a machine's
 * input config, a helper wiring, an /etc drop-in. Write once when absent, then
 * never again - §5's "seeded, then yours" contract - so a rerun cannot clobber
 * an edit. Already present counts as success.
 *
 * The _root variant is for paths outside the user's home (/etc, /usr/share).
 * Pick by who should OWN the result, not by who is running: a file written
 * under the wrong identity is the one failure mode this pair exists to
 * prevent (a root-owned dotfile the user's session cannot rewrite, or a
 * user-owned file under /usr that a package upgrade fights over).
 *
 * content is written verbatim - include the trailing newline. */
int osr_seed_file(const char *dst, const char *content);
int osr_seed_file_root(const char *dst, const char *content);

/* osr_install_theme_layer -- install_theme_layer: the current theme's version
 * of <app>/<name> into dst, whether the theme ships the file itself or the
 * dotfiles template has to be rendered for it. Returns 0 when this theme has
 * neither, which is the caller's cue to fall back to the dotfiles default. */
int osr_install_theme_layer(const char *app, const char *name, const char *dst);

/* osr_module_runtime_run -- compile modules/<name>.c into the user cache when
 * stale, load it into this process, and call its osrm_<name> entry point.
 * Present only in the runtime-module build; static builds dispatch directly. */
#ifdef OSR_RUNTIME_MODULES
int osr_module_runtime_run(const char *name);
#endif

#endif /* OSR_MODULE_H */
