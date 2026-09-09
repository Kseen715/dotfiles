/* lib/module_runtime.c -- on-demand compiler and loader for POSIX C modules.
 *
 * Runtime modules use the same internal API as statically linked modules. The
 * runtime osr executable exports that API with -rdynamic; this unit compiles
 * one module as a shared object, caches it outside the checkout, and loads its
 * osrm_<name> entry point. This is an internal ABI, invalidated by the public
 * module headers and OSR_MODULE_RUNTIME_ABI below.
 *
 * C89 + POSIX.
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#define _DEFAULT_SOURCE 1

#include "common.h"
#include "module.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#define OSR_MODULE_RUNTIME_ABI "1"
#define COMPILER_WORDS 16
#define COMPILE_ARGS 32
#define LOCK_TRIES 600
#define LOCK_STALE_SECONDS 300

static const char *const module_cflags[] = {
    "-std=c89", "-Wall", "-Wextra", "-pedantic", "-O2",
    "-Wno-unused-function", "-fPIC", "-shared"
};
#define MODULE_CFLAGS_COUNT (sizeof(module_cflags) / sizeof(module_cflags[0]))

static int valid_name(const char *name) {
    const unsigned char *p = (const unsigned char *)name;
    if (*p == '\0') return 0;
    for (; *p != '\0'; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
            *p == '-' || *p == '_') continue;
        return 0;
    }
    return 1;
}

static unsigned long hash_bytes(unsigned long h, const char *p, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        h ^= (unsigned char)p[i];
        h *= 16777619UL;
    }
    return h;
}

static int hash_file(unsigned long *h, const char *path) {
    char *buf;
    size_t len;
    buf = slurp(path, &len);
    if (buf == NULL) return 0;
    *h = hash_bytes(*h, buf, len);
    free(buf);
    return 1;
}

static int mkdir_private(const char *path) {
    char *copy;
    char *p;
    int ok = 1;

    copy = (char *)malloc(strlen(path) + 1);
    if (copy == NULL) osr_die_oom();
    strcpy(copy, path);
    for (p = copy + 1; *p != '\0'; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(copy, 0700) != 0 && errno != EEXIST) { ok = 0; break; }
        *p = '/';
    }
    if (ok && mkdir(copy, 0700) != 0 && errno != EEXIST) ok = 0;
    free(copy);
    return ok;
}

static size_t split_words(char *buf, char **out, size_t cap) {
    char *p = buf;
    size_t n = 0;
    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;
        if (n == cap) return 0;
        out[n++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') p++;
        if (*p != '\0') *p++ = '\0';
    }
    return n;
}

static void add_path(Str *out, const char *a, const char *b) {
    str_reset(out);
    str_addz(out, a);
    if (out->len > 0 && out->p[out->len - 1] != '/') str_addc(out, '/');
    str_addz(out, b);
}

static int compile_module(const char *compiler, const char *root,
                          const char *source, const char *output) {
    char *compiler_copy;
    char *words[COMPILER_WORDS];
    char *argv[COMPILE_ARGS];
    size_t n, i, argc = 0;
    Str include;
    Str diagnostics;
    int ok;

    compiler_copy = (char *)malloc(strlen(compiler) + 1);
    if (compiler_copy == NULL) osr_die_oom();
    strcpy(compiler_copy, compiler);
    n = split_words(compiler_copy, words, COMPILER_WORDS);
    if (n == 0) { free(compiler_copy); osr_warn("empty module compiler command"); return 0; }
    for (i = 0; i < n; i++) argv[argc++] = words[i];

    str_init(&include);
    str_addzz(&include, "-I", root, "/lib", (const char *)NULL);
    for (i = 0; i < MODULE_CFLAGS_COUNT; i++) argv[argc++] = (char *)module_cflags[i];
    argv[argc++] = include.p;
    argv[argc++] = (char *)"-o";
    argv[argc++] = (char *)output;
    argv[argc++] = (char *)source;
    argv[argc] = NULL;

    str_init(&diagnostics);
    ok = osr_run_capture_err(argv, &diagnostics);
    if (!ok) {
        str_trim_trailing(&diagnostics, '\n');
        if (diagnostics.len > 0) osr_warnf("module compilation failed: %s", str_text(&diagnostics));
        else osr_warnf("module compilation failed with %s", compiler);
    }
    str_freev(&diagnostics, &include, (Str *)NULL);
    free(compiler_copy);
    return ok;
}

/* wait_for_lock -- the mkdir lock around one cache entry's compile.
 *
 * mkdir is the lock because it is the one atomic create POSIX gives without a
 * file to leave behind. The cost is that a run killed mid-compile leaves the
 * directory standing, and nothing on the system will ever remove it: without
 * the takeover below, every later run of that module waits the full timeout
 * and then fails permanently, on a machine whose only fault was a Ctrl-C.
 *
 * So a lock older than LOCK_STALE_SECONDS is treated as abandoned and removed.
 * The window is far longer than any compile of a single module, and losing the
 * race to remove it is harmless -- the loser simply finds the directory gone or
 * re-created and keeps waiting. */
static int lock_is_stale(const char *lock) {
    struct stat st;
    time_t now = time(NULL);
    if (stat(lock, &st) != 0) return 0;
    return now > st.st_mtime && now - st.st_mtime > LOCK_STALE_SECONDS;
}

static int wait_for_lock(const char *lock) {
    int tries;
    for (tries = 0; tries < LOCK_TRIES; tries++) {
        if (mkdir(lock, 0700) == 0) return 1;
        if (errno != EEXIST) return 0;
        if (lock_is_stale(lock)) {
            osr_warnf("removing stale module lock: %s", lock);
            rmdir(lock);
            continue;
        }
        usleep(10000);
    }
    return 0;
}

static const char *module_compiler(void) {
    const char *configured = env_str("OSR_MODULE_CC", env_str("CC", ""));
    if (*configured != '\0') return configured;
    if (osr_have_cmd("cc")) return "cc";
    if (osr_have_cmd("gcc")) return "gcc";
    if (osr_have_cmd("clang")) return "clang";
    if (osr_have_cmd("tcc")) return "tcc";
    return "cc"; /* Keeps the eventual exec failure specific and actionable. */
}

int osr_module_runtime_run(const char *name) {
    const char *root = osr_mod_root();
    const char *compiler = module_compiler();
    const char *cache_root = env_str("XDG_CACHE_HOME", "");
    struct utsname uts;
    unsigned long hash = 2166136261UL;
    Str source, header, common_header, cache, object, lock, temporary, symbol;
    char key[64];
    int fd;
    size_t i;
    int have_lock = 0;
    void *handle;
    void *address;
    int (*run)(void) = NULL;

    if (!valid_name(name)) { osr_warn("invalid C module name"); return 0; }

    str_initv(&source, &header, &common_header, &cache, &object, &lock, &temporary, &symbol,
        (Str *)NULL);
    add_path(&source, root, "modules/");
    str_addzz(&source, name, ".c", (const char *)NULL);
    add_path(&header, root, "lib/module.h");
    add_path(&common_header, root, "lib/common.h");
    if (!hash_file(&hash, str_text(&source)) ||
        !hash_file(&hash, str_text(&header)) ||
        !hash_file(&hash, str_text(&common_header))) {
        osr_warnf("cannot read runtime module source: %s", str_text(&source));
        goto fail;
    }
    hash = hash_bytes(hash, compiler, strlen(compiler));
    for (i = 0; i < MODULE_CFLAGS_COUNT; i++)
        hash = hash_bytes(hash, module_cflags[i], strlen(module_cflags[i]));
    hash = hash_bytes(hash, OSR_MODULE_RUNTIME_ABI, strlen(OSR_MODULE_RUNTIME_ABI));
    if (uname(&uts) == 0) {
        hash = hash_bytes(hash, uts.sysname, strlen(uts.sysname));
        hash = hash_bytes(hash, uts.machine, strlen(uts.machine));
    }
    sprintf(key, "%08lx", hash);

    if (*cache_root != '\0') str_addz(&cache, cache_root);
    else { str_addz(&cache, osr_mod_home()); str_addz(&cache, "/.cache"); }
    str_addzz(&cache, "/os-rice/modules/abi-", OSR_MODULE_RUNTIME_ABI, (const char *)NULL);
    if (!mkdir_private(str_text(&cache))) {
        osr_warnf("cannot create module cache: %s", str_text(&cache));
        goto fail;
    }
    str_addzz(&object, str_text(&cache), "/", name, "-", key, ".so", (const char *)NULL);

    if (!file_exists(str_text(&object))) {
        str_addzz(&lock, str_text(&object), ".lock", (const char *)NULL);
        if (!wait_for_lock(str_text(&lock))) {
            osr_warnf("cannot lock module cache entry: %s", str_text(&lock));
            goto fail;
        }
        have_lock = 1;
        if (!file_exists(str_text(&object))) {
            str_addzz(&temporary, str_text(&object), ".tmp.XXXXXX", (const char *)NULL);
            fd = mkstemp(temporary.p);
            if (fd < 0) { osr_warn("cannot create temporary module output"); goto fail; }
            close(fd);
            if (!compile_module(compiler, root, str_text(&source), str_text(&temporary))) goto fail;
            if (rename(str_text(&temporary), str_text(&object)) != 0) {
                osr_warn("cannot publish compiled module");
                unlink(str_text(&temporary));
                goto fail;
            }
        }
        rmdir(str_text(&lock));
        have_lock = 0;
    }

    handle = dlopen(str_text(&object), RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        osr_warnf("cannot load module %s: %s", name, dlerror());
        unlink(str_text(&object));
        goto fail;
    }
    str_addz(&symbol, "osrm_");
    {
        const char *p;
        for (p = name; *p != '\0'; p++) str_addc(&symbol, *p == '-' ? '_' : *p);
    }
    dlerror();
    address = dlsym(handle, str_text(&symbol));
    if (address == NULL) {
        const char *error = dlerror();
        osr_warnf("module %s has no %s entry point%s%s", name, str_text(&symbol),
                  error != NULL ? ": " : "", error != NULL ? error : "");
        dlclose(handle);
        goto fail;
    }
    memcpy(&run, &address, sizeof(run));
    str_freev(&source, &header, &common_header, &cache, &object, &lock, &temporary, &symbol,
        (Str *)NULL);
    return run();

fail:
    if (have_lock) rmdir(str_text(&lock));
    if (temporary.len > 0) unlink(str_text(&temporary));
    str_freev(&source, &header, &common_header, &cache, &object, &lock, &temporary, &symbol,
        (Str *)NULL);
    return 0;
}
