/* modules/lcc.c -- lcc, the retargetable ANSI C compiler (drh/lcc, 1995-2002).
 *
 * No distro packages lcc (it has been abandoned since 2002), so this module
 * is the only route to it: it fetches the upstream tarball, builds it with
 * the box's own compiler, installs the whole tree under ~/.local/share/lcc,
 * and symlinks `lcc` onto PATH -- the driver bakes its prefix in at install
 * time, so a bare `lcc` in a terminal just works.
 *
 * WHY THE PATCHES -- lcc is an ANSI C89 compiler on a 2026 glibc host, and
 * every quirk here exists because of that. The patched driver and the one
 * header rcc cannot parse are shipped next to this file (modules/src/
 * lcc-linux.c, modules/src/lcc-struct_mutex.h) so the diff is reviewable.
 * Verified against glibc 2.4x / gcc 15:
 *
 *   - lcc's only x86 backend is 32-bit i386 (elf_i386, /lib/ld-linux.so.2),
 *     so the build needs gcc -m32 and 32-bit dev libraries. That is the
 *     `multilib` logical package, installed only when a real -m32 probe
 *     fails, and the driver links the 32-bit crt files from /usr/lib32.
 *   - gcc 15 hard-errors on implicit-int, so the 2002 sources compile with
 *     CC='cc -std=gnu89'; the runtime library (liblcc.a) is built with
 *     CC='cc -m32 -std=gnu89' because the linker that consumes it is 32-bit.
 *   - cpp-15 defaults to gnu17, which defines __STDC_VERSION__ -- a build
 *     tool like nob.c then picks its C99 header instead of its C89 one. The
 *     driver's cpp line is forced to -std=c89.
 *   - -U__GNUC__ cannot undefine a builtin macro in cpp-15, so glibc still
 *     emits __attribute__/restrict/inline gcc-isms that rcc's C89 parser
 *     rejects; the driver neutralizes them with -D stubs.
 *   - modern GNU as defaults to 64-bit; the driver passes -32.
 *
 * Idempotent (SS2): when ~/.local/share/lcc/lcc already exists the whole
 * build is skipped. Not themable. C89.
 */
#define _POSIX_C_SOURCE 200809L
#include "../lib/module.h"
#include "../lib/fetch.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define LCC_URL "https://github.com/drh/lcc/archive/refs/heads/master.tar.gz"
#define LCC_REL ".local/share/lcc"

/* join -- a/b into out (out is reset). */
static void join(Str *out, const char *a, const char *b) {
    str_reset(out);
    str_addz(out, a);
    if (out->len > 0 && out->p[out->len - 1] != '/') str_addc(out, '/');
    str_addz(out, b);
}

/* is_exec -- 1 when path exists and is executable by its owner. */
static int is_exec(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && (st.st_mode & S_IXUSR) != 0;
}

/* trim_nl -- drop the single trailing newline osr_run_capture leaves. */
static void trim_nl(Str *s) {
    if (s->len > 0 && s->p[s->len - 1] == '\n') {
        s->p[s->len - 1] = '\0';
        s->len--;
    }
}

/* gcc_print -- `gcc [-m32] -print-file-name=<what>`, returning 1 when gcc
 * names a real path for it. A bare name (no '/') is gcc's way of saying it
 * has no such file, which is exactly how a missing 32-bit multilib reports
 * itself. */
static int gcc_print(Str *out, int want_m32, const char *what) {
    char *argv[4];
    int n = 0;
    argv[n++] = (char *)"gcc";
    if (want_m32) argv[n++] = (char *)"-m32";
    argv[n++] = (char *)what;
    argv[n] = NULL;
    str_reset(out);
    if (!osr_run_capture(argv, out)) return 0;
    trim_nl(out);
    return out->len > 0 && strchr(out->p, '/') != NULL;
}

/* sed_escape -- copy s into out (reset first), backslash-escaping the three
 * characters sed reads specially in a replacement text: &, \ and the #
 * delimiter this module's sed commands use. */
static void sed_escape(Str *out, const char *s) {
    const char *p;
    str_reset(out);
    for (p = s; *p != '\0'; p++) {
        if (*p == '&' || *p == '\\' || *p == '#') str_addc(out, '\\');
        str_addc(out, *p);
    }
}

/* run_sh_user -- one osr_run_step_user around `sh -c <script>`. */
static int run_sh_user(const char *desc, const char *script) {
    char *argv[4];
    argv[0] = (char *)"sh"; argv[1] = (char *)"-c";
    argv[2] = (char *)script; argv[3] = NULL;
    return osr_run_step_user(desc, argv);
}

int osrm_lcc(void) {
    Str prefix, bin, src, srclcc, patch, gz, script, esc;
    Str crtbegin, crtend, libgcc, incdir;
    int ok = 1;

    str_init(&prefix); str_init(&bin); str_init(&src); str_init(&srclcc);
    str_init(&patch); str_init(&gz); str_init(&script); str_init(&esc);
    str_init(&crtbegin); str_init(&crtend); str_init(&libgcc); str_init(&incdir);

    join(&prefix, osr_mod_home(), LCC_REL);
    join(&bin, osr_mod_home(), ".local/bin");
    join(&src, str_text(&prefix), "src");
    join(&srclcc, str_text(&src), "lcc");
    join(&gz, str_text(&src), "lcc.tar.gz");

    /* Idempotency probe (SS2): the driver already in place means the whole
     * build is skipped -- no network, no compiler, no rerun cost. */
    join(&script, str_text(&prefix), "lcc");
    if (is_exec(str_text(&script))) {
        osr_infof("lcc already installed at %s - skipping", str_text(&prefix));
        goto out;
    }

    /* The one hard dependency: a C toolchain. `build` maps to build-essential
     * / base-devel / gcc+make per distro. */
    {
        static const char *const pkgs[] = { "build", NULL };
        if (!osr_pkg_install_step("Installing lcc build toolchain", pkgs)) {
            ok = 0; goto out;
        }
    }

    /* 32-bit dev libraries, gated on a REAL probe: only a box whose
     * `gcc -m32` cannot name crtbegin.o needs the `multilib` package. */
    if (!gcc_print(&crtbegin, 1, "-print-file-name=crtbegin.o")) {
        static const char *const ml[] = { "multilib", NULL };
        osr_pkg_install_step_try("Installing 32-bit dev libraries", ml);
        if (!gcc_print(&crtbegin, 1, "-print-file-name=crtbegin.o")) {
            osr_warnf("lcc needs 32-bit multilib dev libraries (gcc -m32) and "
                      "none could be installed - skipping lcc");
            ok = 0; goto out;
        }
    }

    /* A downloader for the source tarball. */
    osr_fetch_ensure();
    if (osr_fetch_backend()[0] == '\0') {
        osr_warnf("lcc: no downloader available (curl/wget) - skipping");
        ok = 0; goto out;
    }

    /* Clean slate, then the directory skeleton. A partial failed run must
     * not leave half-built objects behind for the rerun to reuse. */
    str_reset(&script);
    str_addz(&script, "rm -rf ");
    str_addz(&script, str_text(&prefix));
    str_addz(&script, " && mkdir -p ");
    str_addz(&script, str_text(&src));
    str_addz(&script, " ");
    str_addz(&script, str_text(&prefix));
    str_addz(&script, "/include ");
    str_addz(&script, str_text(&prefix));
    str_addz(&script, "/gcc ");
    str_addz(&script, str_text(&prefix));
    str_addz(&script, "/include-patch/bits");
    if (!run_sh_user("Preparing lcc install directory", str_text(&script))) {
        ok = 0; goto out;
    }

    /* Fetch + extract the upstream tarball. */
    if (!osr_fetch_download(LCC_URL, str_text(&gz), 0)) {
        osr_warnf("lcc: failed to download the source tarball");
        ok = 0; goto out;
    }
    str_reset(&script);
    str_addz(&script, "cd ");
    str_addz(&script, str_text(&src));
    str_addz(&script, " && tar -xzf lcc.tar.gz && mv lcc-master lcc");
    if (!run_sh_user("Extracting lcc source", str_text(&script))) {
        ok = 0; goto out;
    }

    /* Ship the patched driver, baking the prefix into @LCCPREFIX@. */
    join(&patch, osr_mod_root(), "modules/src/lcc-linux.c");
    sed_escape(&esc, str_text(&prefix));
    str_reset(&script);
    str_addz(&script, "cp ");
    str_addz(&script, str_text(&patch));
    str_addz(&script, " ");
    str_addz(&script, str_text(&srclcc));
    str_addz(&script, "/etc/linux.c && sed -i 's#@LCCPREFIX@#");
    str_addz(&script, str_text(&esc));
    str_addz(&script, "/#g' ");
    str_addz(&script, str_text(&srclcc));
    str_addz(&script, "/etc/linux.c");
    if (!run_sh_user("Patching lcc driver for this host", str_text(&script))) {
        ok = 0; goto out;
    }

    /* The overlay header rcc cannot parse, on the driver's include path. */
    join(&patch, osr_mod_root(), "modules/src/lcc-struct_mutex.h");
    str_reset(&script);
    str_addz(&script, "cp ");
    str_addz(&script, str_text(&patch));
    str_addz(&script, " ");
    str_addz(&script, str_text(&prefix));
    str_addz(&script, "/include-patch/bits/struct_mutex.h");
    if (!run_sh_user("Installing patched glibc header", str_text(&script))) {
        ok = 0; goto out;
    }

    /* lcc's own headers plus the gcc support dir the driver links against.
     * The four gcc paths come from `gcc -print-file-name`, so this follows
     * whatever gcc version and multilib layout the box actually has. */
    gcc_print(&crtbegin, 1, "-print-file-name=crtbegin.o");
    gcc_print(&crtend,   1, "-print-file-name=crtend.o");
    gcc_print(&libgcc,   1, "-print-file-name=libgcc.a");
    gcc_print(&incdir,   0, "-print-file-name=include");
    str_reset(&script);
    str_addz(&script, "cp ");
    str_addz(&script, str_text(&srclcc));
    str_addz(&script, "/include/x86/linux/* ");
    str_addz(&script, str_text(&prefix));
    str_addz(&script, "/include/ && ln -sf /usr/bin/cpp ");
    str_addz(&script, str_text(&prefix));
    str_addz(&script, "/gcc/cpp && ln -sf \"");
    str_addz(&script, str_text(&incdir));
    str_addz(&script, "\" ");
    str_addz(&script, str_text(&prefix));
    str_addz(&script, "/gcc/include && ln -sf \"");
    str_addz(&script, str_text(&crtbegin));
    str_addz(&script, "\" ");
    str_addz(&script, str_text(&prefix));
    str_addz(&script, "/gcc/crtbegin.o && ln -sf \"");
    str_addz(&script, str_text(&crtend));
    str_addz(&script, "\" ");
    str_addz(&script, str_text(&prefix));
    str_addz(&script, "/gcc/crtend.o && ln -sf \"");
    str_addz(&script, str_text(&libgcc));
    str_addz(&script, "\" ");
    str_addz(&script, str_text(&prefix));
    str_addz(&script, "/gcc/libgcc.a");
    if (!run_sh_user("Installing lcc headers and gcc support dir", str_text(&script))) {
        ok = 0; goto out;
    }

    /* Build. The runtime library first and 32-bit (the driver links 32-bit
     * binaries against it); the driver, compiler proper, preprocessor and
     * profile tools are host-64-bit and compile with -std=gnu89 because gcc
     * 15 treats the 2002 code's implicit-int as an error. */
    str_reset(&script);
    str_addz(&script, "cd ");
    str_addz(&script, str_text(&srclcc));
    str_addz(&script, " && make BUILDDIR=");
    str_addz(&script, str_text(&prefix));
    str_addz(&script, " CC=\"cc -m32 -std=gnu89\" CFLAGS=\"-g\" liblcc");
    if (!run_sh_user("Building lcc runtime library (32-bit)", str_text(&script))) {
        ok = 0; goto out;
    }
    str_reset(&script);
    str_addz(&script, "cd ");
    str_addz(&script, str_text(&srclcc));
    str_addz(&script, " && make BUILDDIR=");
    str_addz(&script, str_text(&prefix));
    str_addz(&script, " HOSTFILE=etc/linux.c CC=\"cc -std=gnu89\" CFLAGS=\"-g\" "
                      "rcc lburg cpp lcc bprint");
    if (!run_sh_user("Building lcc compiler", str_text(&script))) {
        ok = 0; goto out;
    }

    /* Sanity: the driver must compile AND run a hello in one shot. */
    str_reset(&script);
    str_addz(&script, str_text(&src));
    str_addz(&script, "/hello.c");
    osr_write_user(str_text(&script),
        "#include <stdio.h>\nint main(void){printf(\"lcc ok\\n\");return 0;}\n");
    str_reset(&script);
    str_addz(&script, "cd ");
    str_addz(&script, str_text(&src));
    str_addz(&script, " && ");
    str_addz(&script, str_text(&prefix));
    str_addz(&script, "/lcc -o hello hello.c && ./hello");
    if (!run_sh_user("Verifying lcc on a hello world", str_text(&script))) {
        osr_warnf("lcc built but its hello world did not run - something is off");
        ok = 0; goto out;
    }

    /* PATH. ~/.local/bin is on the profile's PATH (the 00-env.zsh layer), so
     * a bare `lcc` in any terminal now resolves to the installed driver. */
    str_reset(&script);
    str_addz(&script, "mkdir -p ");
    str_addz(&script, str_text(&bin));
    str_addz(&script, " && ln -sf ");
    str_addz(&script, str_text(&prefix));
    str_addz(&script, "/lcc ");
    str_addz(&script, str_text(&bin));
    str_addz(&script, "/lcc");
    if (!run_sh_user("Linking lcc onto PATH", str_text(&script))) {
        ok = 0; goto out;
    }

    osr_successf("lcc installed at %s - type `lcc` to compile (32-bit i386)",
                 str_text(&prefix));

out:
    str_free(&prefix); str_free(&bin); str_free(&src); str_free(&srclcc);
    str_free(&patch); str_free(&gz); str_free(&script); str_free(&esc);
    str_free(&crtbegin); str_free(&crtend); str_free(&libgcc); str_free(&incdir);
    return ok;
}
