/*
 * nob89.h -- a C90 (aka C89) backend for nob.c.
 *
 * nob.h is C99 (// comments, for-loop declarations, variadic macros,
 * compound literals, <stdint.h>/<stdbool.h>), so on a host whose compiler
 * only speaks C89 the normal build script cannot even bootstrap itself.
 * nob89.h is that fallback: it reimplements the slice of nob.h that nob.c
 * actually uses, under the same nob_/Nob_/NOB_ names, in strict C90, with
 * one deliberate simplification:
 *
 *   anything nob.c asks to run asynchronously (.async = ...) is run
 *   synchronously instead. nob_procs_flush() is then a no-op that always
 *   succeeds, and the build is just serial.
 *
 * Because the two headers expose an identical subset, the same nob.c builds
 * against the real nob.h (C99) with no changes -- the build logic is not
 * duplicated. nob.c picks this backend automatically from __STDC_VERSION__
 * (undefined, or < 199901L, in C89), so compiling with `-std=c89` is enough;
 * `-DNOB89` forces it explicitly:
 *
 * Everything here is plain C90: no // comments, no declarations after
 * statements, no variadic macros, no compound literals, no designated
 * initializers, no stdint/stdbool/long long. `bool` is an int typedef.
 */
#ifndef NOB89_H_
#define NOB89_H_

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

/* C90 has no <stdbool.h>; nob.c's bool/true/false come from here. C23 made
 * bool/true/false keywords, so only provide them for C89/C99/C11. */
#if defined(__cplusplus)
/* C++ already has bool/true/false; do nothing. */
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
/* C23 keywords; do nothing. */
#else
#ifndef bool
typedef int bool;
#endif
#ifndef true
#define true 1
#define false 0
#endif
#endif

#define NOB_ASSERT assert
#define NOB_UNUSED(value) (void)(value)

#define NOB_DA_INIT_CAP 256
#define nob_da_append(da, item)                                             \
    do {                                                                     \
        if ((da)->count >= (da)->capacity) {                                 \
            (da)->capacity = (da)->capacity ? (da)->capacity * 2 : NOB_DA_INIT_CAP; \
            (da)->items = realloc((da)->items, (da)->capacity * sizeof(*(da)->items)); \
            NOB_ASSERT((da)->items != NULL);                                 \
        }                                                                    \
        (da)->items[(da)->count++] = (item);                                 \
    } while (0)

typedef enum {
    NOB_INFO,
    NOB_WARNING,
    NOB_ERROR,
    NOB_NO_LOGS
} Nob_Log_Level;

typedef void (Nob_Log_Handler)(Nob_Log_Level level, const char *fmt, va_list args);

typedef struct {
    const char **items;
    size_t count;
    size_t capacity;
} Nob_Cmd;

typedef struct {
    char *items;
    size_t count;
    size_t capacity;
} Nob_String_Builder;

typedef struct {
    const char **items;
    size_t count;
    size_t capacity;
} Nob_File_Paths;

typedef int Nob_Proc;
typedef struct {
    Nob_Proc *items;
    size_t count;
    size_t capacity;
} Nob_Procs;

typedef enum {
    NOB_FILE_REGULAR = 0,
    NOB_FILE_DIRECTORY,
    NOB_FILE_SYMLINK,
    NOB_FILE_OTHER
} Nob_File_Type;

typedef enum {
    NOB_WALK_CONT,
    NOB_WALK_SKIP,
    NOB_WALK_STOP
} Nob_Walk_Action;

typedef struct {
    const char *path;
    Nob_File_Type type;
    size_t level;
    void *data;
    Nob_Walk_Action *action;
} Nob_Walk_Entry;

typedef int (*Nob_Walk_Func)(Nob_Walk_Entry entry);

typedef struct {
    Nob_Procs *async;
    size_t max_procs;
    int dont_reset;
    const char *stdin_path;
    const char *stdout_path;
    const char *stderr_path;
} Nob_Cmd_Opt;

extern Nob_Log_Level nob_minimal_log_level;

void nob_log(Nob_Log_Level level, const char *fmt, ...);
void nob_set_log_handler(Nob_Log_Handler *handler);
void nob_default_log_handler(Nob_Log_Level level, const char *fmt, va_list args);

void nob_cmd_append(Nob_Cmd *cmd, const char *arg);
int  nob_cmd_run(Nob_Cmd *cmd);
int  nob_cmd_run_opt(Nob_Cmd *cmd, Nob_Cmd_Opt opt);
int  nob_procs_flush(Nob_Procs *procs);

int  nob_mkdir_if_not_exists(const char *path);
int  nob_file_exists(const char *path);
int  nob_needs_rebuild(const char *output_path, const char **input_paths, size_t input_paths_count);
int  nob_needs_rebuild1(const char *output_path, const char *input_path);
int  nob_write_entire_file(const char *path, const void *data, size_t size);
int  nob_read_entire_file(const char *path, Nob_String_Builder *sb);
int  nob_delete_file(const char *path);
int  nob_set_current_dir(const char *path);
const char *nob_path_name(const char *path);

char *nob_temp_sprintf(const char *fmt, ...);
char *nob_temp_strdup(const char *cstr);

int nob_walk_dir(const char *root, Nob_Walk_Func func);

void nob_go_rebuild_urself(int argc, char **argv, const char *source_path);

#define nob_sb_append_null(sb) nob_da_append((sb), '\0')
#define nob_sb_free(sb) free((sb).items)
#define nob_shift(xs, xs_sz) (NOB_ASSERT((xs_sz) > 0), (xs_sz)--, *(xs)++)
#define NOB_GO_REBUILD_URSELF(argc, argv) nob_go_rebuild_urself((argc), (argv), __FILE__)

#endif /* NOB89_H_ */

#ifdef NOB89_IMPLEMENTATION

#define NOB89_TMP_CAP 4096
#define NOB89_FMT_CAP 8192
#define NOB89_TEMP_CAPACITY (8*1024*1024)

#ifdef _WIN32
#  if defined(_MSC_VER)
typedef struct _stat Nob89_Stat;
#    define NOB89_STAT _stat
#  else
typedef struct stat Nob89_Stat;
#    define NOB89_STAT stat
#  endif
#else
typedef struct stat Nob89_Stat;
#  define NOB89_STAT stat
#endif

static char nob89_temp[NOB89_TEMP_CAPACITY];
static size_t nob89_temp_size = 0;
static Nob_Log_Handler *nob89_log_handler = &nob_default_log_handler;

Nob_Log_Level nob_minimal_log_level = NOB_INFO;

/* nob89_temp_alloc -- bump allocation from a fixed arena. Pointers never
 * move and stay valid for the whole run, which is what nob.c's deps list
 * (nob_temp_strdup) relies on. */
static char *nob89_temp_alloc(size_t size)
{
    size_t aligned = (size + 7) & ~(size_t)7;
    char *p;
    if (nob89_temp_size + aligned > NOB89_TEMP_CAPACITY) return NULL;
    p = &nob89_temp[nob89_temp_size];
    nob89_temp_size += aligned;
    return p;
}

void nob_set_log_handler(Nob_Log_Handler *handler)
{
    nob89_log_handler = handler;
}

void nob_default_log_handler(Nob_Log_Level level, const char *fmt, va_list args)
{
    const char *prefix;
    char buf[NOB89_TMP_CAP];

    if (level < nob_minimal_log_level) return;
    switch (level) {
    case NOB_INFO:    prefix = "[INFO] ";    break;
    case NOB_WARNING: prefix = "[WARNING] "; break;
    case NOB_ERROR:   prefix = "[ERROR] ";   break;
    case NOB_NO_LOGS: return;
    default:          prefix = "";           break;
    }
    vsprintf(buf, fmt, args);
    fprintf(stderr, "%s%s\n", prefix, buf);
}

void nob_log(Nob_Log_Level level, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    nob89_log_handler(level, fmt, args);
    va_end(args);
}

void nob_cmd_append(Nob_Cmd *cmd, const char *arg)
{
    nob_da_append(cmd, arg);
}

/* nob89_cmd_render -- join argv with spaces, single-quoting arguments that
 * contain whitespace or quotes. The result is what nob.c's brief_cmd()
 * parses to produce the autoconf-style one-line-per-output echo. */
static void nob89_cmd_render(const Nob_Cmd *cmd, char *out, size_t out_cap)
{
    size_t i;
    size_t len;
    int quote;
    const char *p;
    const char *arg;
    size_t arglen;

    len = 0;
    out[0] = '\0';

    for (i = 0; i < cmd->count; i++) {
        arg = cmd->items[i];
        if (arg == NULL) continue;

        quote = 0;
        for (p = arg; *p; p++) {
            if (*p == ' ' || *p == '\t' || *p == '\'' || *p == '\"') {
                quote = 1;
                break;
            }
        }
        if (arg[0] == '\0') quote = 1;
        arglen = strlen(arg);

        if (i > 0 && len + 1 < out_cap) out[len++] = ' ';
        if (quote && len + 1 < out_cap) out[len++] = '\'';
        if (len + arglen < out_cap) {
            memcpy(out + len, arg, arglen);
            len += arglen;
        }
        if (quote && len + 1 < out_cap) out[len++] = '\'';
        out[len] = '\0';
    }
}

int nob_cmd_run_opt(Nob_Cmd *cmd, Nob_Cmd_Opt opt)
{
    char render[NOB89_TMP_CAP];
    int result = 1;
#ifdef _WIN32
    int status;
#else
    int fdin;
    int fdout;
    int fderr;
    pid_t pid;
    int wstatus;
#endif

    if (cmd->count == 0) {
        nob_log(NOB_ERROR, "empty command");
        return 0;
    }

    nob89_cmd_render(cmd, render, sizeof(render));
    nob_log(NOB_INFO, "CMD: %s", render);

    /* NULL-terminate argv for exec; the count is restored below. */
    nob_da_append(cmd, NULL);

#ifdef _WIN32
    /* async and redirect degrade away: run in the current console. */
    status = (int)_spawnvp(_P_WAIT, cmd->items[0], (const char *const *)cmd->items);
    if (status == -1) {
        nob_log(NOB_ERROR, "could not run `%s`", cmd->items[0]);
        result = 0;
    } else if (status != 0) {
        nob_log(NOB_ERROR, "command exited with code %d", status);
        result = 0;
    }
#else
    fdin = fdout = fderr = -1;
    if (opt.stdin_path)  fdin  = open(opt.stdin_path,  O_RDONLY);
    if (opt.stdout_path) fdout = open(opt.stdout_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (opt.stderr_path) fderr = open(opt.stderr_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    pid = fork();
    if (pid < 0) {
        nob_log(NOB_ERROR, "fork failed");
        result = 0;
    } else if (pid == 0) {
        if (fdin  >= 0) dup2(fdin,  STDIN_FILENO);
        if (fdout >= 0) dup2(fdout, STDOUT_FILENO);
        if (fderr >= 0) dup2(fderr, STDERR_FILENO);
        execvp(cmd->items[0], (char *const *)cmd->items);
        nob_log(NOB_ERROR, "could not exec `%s`", cmd->items[0]);
        exit(127);
    } else {
        if (fdin  >= 0) close(fdin);
        if (fdout >= 0) close(fdout);
        if (fderr >= 0) close(fderr);
        if (waitpid(pid, &wstatus, 0) < 0) {
            nob_log(NOB_ERROR, "waitpid failed");
            result = 0;
        } else if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0) {
            nob_log(NOB_ERROR, "command failed");
            result = 0;
        }
    }
#endif

    if (!opt.dont_reset) cmd->count = 0;
    else cmd->count--;
    return result;
}

int nob_cmd_run(Nob_Cmd *cmd)
{
    Nob_Cmd_Opt opt;
    memset(&opt, 0, sizeof(opt));
    return nob_cmd_run_opt(cmd, opt);
}

int nob_procs_flush(Nob_Procs *procs)
{
    NOB_UNUSED(procs);
    return 1;
}

int nob_mkdir_if_not_exists(const char *path)
{
#ifdef _WIN32
    if (_mkdir(path) < 0) {
        if (errno == EEXIST) return 1;
        nob_log(NOB_ERROR, "could not create directory `%s`", path);
        return 0;
    }
#else
    if (mkdir(path, 0755) < 0) {
        if (errno == EEXIST) return 1;
        nob_log(NOB_ERROR, "could not create directory `%s`", path);
        return 0;
    }
#endif
    return 1;
}

int nob_file_exists(const char *path)
{
    Nob89_Stat st;
    return NOB89_STAT(path, &st) == 0;
}

int nob_needs_rebuild(const char *output_path, const char **input_paths, size_t input_paths_count)
{
    Nob89_Stat out_st;
    size_t i;

    if (NOB89_STAT(output_path, &out_st) < 0) return 1;
    for (i = 0; i < input_paths_count; i++) {
        Nob89_Stat in_st;
        if (NOB89_STAT(input_paths[i], &in_st) < 0) return -1;
        if (in_st.st_mtime > out_st.st_mtime) return 1;
    }
    return 0;
}

int nob_needs_rebuild1(const char *output_path, const char *input_path)
{
    return nob_needs_rebuild(output_path, &input_path, 1);
}

int nob_write_entire_file(const char *path, const void *data, size_t size)
{
    FILE *f;
    f = fopen(path, "wb");
    if (f == NULL) {
        nob_log(NOB_ERROR, "could not open `%s` for writing", path);
        return 0;
    }
    if (size > 0 && fwrite(data, 1, size, f) != size) {
        nob_log(NOB_ERROR, "could not write `%s`", path);
        fclose(f);
        return 0;
    }
    fclose(f);
    return 1;
}

int nob_read_entire_file(const char *path, Nob_String_Builder *sb)
{
    FILE *f;
    long n;
    size_t need;

    f = fopen(path, "rb");
    if (f == NULL) return 0;
    if (fseek(f, 0, SEEK_END) < 0) { fclose(f); return 0; }
    n = ftell(f);
    if (n < 0) { fclose(f); return 0; }
    if (fseek(f, 0, SEEK_SET) < 0) { fclose(f); return 0; }

    need = sb->count + (size_t)n;
    if (need > sb->capacity) {
        sb->capacity = need;
        sb->items = (char *)realloc(sb->items, sb->capacity);
        NOB_ASSERT(sb->items != NULL);
    }
    if (n > 0 && fread(sb->items + sb->count, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        return 0;
    }
    sb->count = need;
    fclose(f);
    return 1;
}

int nob_delete_file(const char *path)
{
    return remove(path) == 0;
}

int nob_set_current_dir(const char *path)
{
#ifdef _WIN32
    return SetCurrentDirectoryA(path) ? 1 : 0;
#else
    return chdir(path) == 0;
#endif
}

const char *nob_path_name(const char *path)
{
#ifdef _WIN32
    const char *p1 = strrchr(path, '/');
    const char *p2 = strrchr(path, '\\');
    const char *p = (p1 > p2) ? p1 : p2;
    return p ? p + 1 : path;
#else
    const char *p = strrchr(path, '/');
    return p ? p + 1 : path;
#endif
}

char *nob_temp_sprintf(const char *fmt, ...)
{
    char scratch[NOB89_FMT_CAP];
    size_t n;
    char *p;
    va_list args;

    va_start(args, fmt);
    vsprintf(scratch, fmt, args);
    va_end(args);

    n = strlen(scratch);
    p = nob89_temp_alloc(n + 1);
    NOB_ASSERT(p != NULL);
    memcpy(p, scratch, n + 1);
    return p;
}

char *nob_temp_strdup(const char *cstr)
{
    return nob_temp_sprintf("%s", cstr);
}

static Nob_File_Type nob_get_file_type(const char *path)
{
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) return (Nob_File_Type)-1;
    if (attr & FILE_ATTRIBUTE_DIRECTORY) return NOB_FILE_DIRECTORY;
    return NOB_FILE_REGULAR;
#else
    struct stat st;
    /* stat (not lstat): lstat is hidden in strict C89 on glibc, and the
     * walk only distinguishes directories from everything else, so following
     * symlinks here is the behaviour we want anyway. */
    if (stat(path, &st) < 0) return (Nob_File_Type)-1;
    if (S_ISREG(st.st_mode)) return NOB_FILE_REGULAR;
    if (S_ISDIR(st.st_mode)) return NOB_FILE_DIRECTORY;
    return NOB_FILE_OTHER;
#endif
}

#ifdef _WIN32
static int nob89_walk_dir_impl(const char *path, Nob_Walk_Func func, size_t level)
{
    char *pattern;
    WIN32_FIND_DATAA fd;
    HANDLE h;
    Nob_File_Type type;
    Nob_Walk_Action action = NOB_WALK_CONT;
    Nob_Walk_Entry entry;

    type = nob_get_file_type(path);
    if (type < 0) return 0;

    entry.path = path;
    entry.type = type;
    entry.level = level;
    entry.data = NULL;
    entry.action = &action;
    if (!func(entry)) return 0;
    if (action != NOB_WALK_CONT) return 1;
    if (type != NOB_FILE_DIRECTORY) return 1;

    pattern = nob_temp_sprintf("%s\\*", path);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    for (;;) {
        const char *name = fd.cFileName;
        if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
            char *child = nob_temp_sprintf("%s\\%s", path, name);
            if (!nob89_walk_dir_impl(child, func, level + 1)) { FindClose(h); return 0; }
        }
        if (!FindNextFileA(h, &fd)) break;
    }
    FindClose(h);
    return 1;
}
#else
static int nob89_walk_dir_impl(const char *path, Nob_Walk_Func func, size_t level)
{
    DIR *d;
    struct dirent *ent;
    Nob_File_Type type;
    Nob_Walk_Action action = NOB_WALK_CONT;
    Nob_Walk_Entry entry;

    type = nob_get_file_type(path);
    if (type < 0) return 0;

    entry.path = path;
    entry.type = type;
    entry.level = level;
    entry.data = NULL;
    entry.action = &action;
    if (!func(entry)) return 0;
    if (action != NOB_WALK_CONT) return 1;
    if (type != NOB_FILE_DIRECTORY) return 1;

    d = opendir(path);
    if (d == NULL) return 0;
    for (;;) {
        char *child;
        ent = readdir(d);
        if (ent == NULL) break;
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        child = nob_temp_sprintf("%s/%s", path, ent->d_name);
        if (!nob89_walk_dir_impl(child, func, level + 1)) { closedir(d); return 0; }
    }
    closedir(d);
    return 1;
}
#endif

int nob_walk_dir(const char *root, Nob_Walk_Func func)
{
    return nob89_walk_dir_impl(root, func, 0);
}

/* nob89_cc_is_faucc -- does the $CC that would rebuild us name faucc
 * (FAUcc), with or without a path? faucc rejects -std=c89 outright, so the
 * C89 pin below must not go on its command line or the rebuild dies on
 * "Unknown option -std=c89" before cc1 even runs. (faucc still cannot
 * actually compile this header -- its cc1 predates what glibc headers do --
 * but the command line has to be valid first.) */
static int nob89_cc_is_faucc(const char *c)
{
    const char *base = c;
    const char *p;
    size_t len;
    for (p = c; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    len = strlen(base);
    return len == 5 && memcmp(base, "faucc", 5) == 0;
}

/* nob89_cc_is_lcc -- does the $CC that would rebuild us name lcc (Fraser &
 * Hanson's retargetable compiler), with or without a path? lcc's driver does
 * not understand -std=c89: with no -c on the line (a link) it silently
 * treats the flag as a linker input file, so GNU ld dies on "unrecognized
 * option '-std=c89'". The patched driver (modules/src/lcc-linux.c) already
 * forces -std=c89 on its own cpp line and rcc is a C89 compiler natively,
 * so the C89 pin below can be skipped for it the same way it is for faucc. */
static int nob89_cc_is_lcc(const char *c)
{
    const char *base = c;
    const char *p;
    size_t len;
    for (p = c; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    len = strlen(base);
    return len == 3 && memcmp(base, "lcc", 3) == 0;
}

void nob_go_rebuild_urself(int argc, char **argv, const char *source_path)
{
    const char *self;
    const char *ins[1];
    const char *cc;
    Nob_Cmd cmd;
    int need;

    if (argc < 1) return;
    self = argv[0];
    ins[0] = source_path;

    need = nob_needs_rebuild(self, ins, 1);
    if (need == 0) return;
    if (need < 0) return;

    cc = getenv("CC");
    if (cc == NULL || cc[0] == '\0') cc = "cc";

    /* Rebuild in the compiler's own default mode and let nob.c pick its
     * backend from __STDC_VERSION__, rather than pinning -DNOB89 here. The
     * pin was sticky in the worst way: once a C89 host (or one stray
     * `-DNOB89`) produced a nob, every later self-rebuild carried the flag
     * forward, so a nob on a perfectly good C99 host kept running this
     * serial backend and the build never used more than one core. $CC
     * selects the compiler nob *spawns*; nothing but this line selects the
     * backend nob itself is built with.
     *
     * A real C89 compiler reports __STDC_VERSION__ < 199901L and lands on
     * this header anyway -- and unforced, it lands there in C89 mode, where
     * `typedef int bool` is what nob.c's `bool (*)(Nob_Walk_Entry)`
     * callbacks are checked against. (That is what -std=c89 used to buy:
     * -DNOB89 dragged this header into a gnu17 compile, glibc's
     * <stdbool.h> made `bool` _Bool, and the callbacks stopped matching
     * Nob_Walk_Func. No define, no mismatch, no flag.)
     *
     * The fallback below covers the one host the default mode can lose:
     * a compiler modern enough to claim C99 but not to digest nob.h. */
    memset(&cmd, 0, sizeof(cmd));
    nob_cmd_append(&cmd, cc);
    nob_cmd_append(&cmd, "-o");
    nob_cmd_append(&cmd, self);
    nob_cmd_append(&cmd, source_path);
    if (!nob_cmd_run(&cmd)) {
        /* Default mode could not build nob.c. Retry pinned to this
         * backend, which is what got us here and is known to work. faucc
         * does not accept -std=c89 at all, and lcc's driver reads it as a
         * linker input on a link line; both already compile as C89. */
        nob_log(NOB_WARNING, "rebuild failed in %s's default mode; retrying with -DNOB89", cc);
        memset(&cmd, 0, sizeof(cmd));
        nob_cmd_append(&cmd, cc);
        nob_cmd_append(&cmd, "-DNOB89");
        if (!nob89_cc_is_faucc(cc) && !nob89_cc_is_lcc(cc)) nob_cmd_append(&cmd, "-std=c89");
        nob_cmd_append(&cmd, "-o");
        nob_cmd_append(&cmd, self);
        nob_cmd_append(&cmd, source_path);
        if (!nob_cmd_run(&cmd)) exit(1);
    }

#ifdef _WIN32
    exit((int)_spawnv(_P_WAIT, self, (const char *const *)argv));
#else
    execv(self, argv);
    nob_log(NOB_ERROR, "execv failed after self-rebuild");
    exit(1);
#endif
}

#endif /* NOB89_IMPLEMENTATION */
