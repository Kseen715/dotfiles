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
int osr_state_main(int argc, char **argv);    /* lib/state.sh */
int osr_user_main(int argc, char **argv);     /* lib/user.sh */
int osr_theme_main(int argc, char **argv);    /* lib/theme.sh */
int osr_detect_main(int argc, char **argv);   /* lib/detect.sh */
int osr_install_main(int argc, char **argv);  /* install.sh */
int osr_module_main(int argc, char **argv);   /* the Linux C modules */
int osr_pkg_main(int argc, char **argv);      /* lib/pkg.sh */
int osr_net_main(int argc, char **argv);      /* lib/net.sh */
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
int osr_testrun_main(int argc, char **argv);  /* test/run.sh */

#endif /* OSR_CMDS_H */
