/* lib/winbin.c -- see lib/winbin.h. C89. */
#include "winbin.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * pure helpers -- no I/O, so the rules that decide WHICH file gets
 * downloaded and WHAT it is are unit-testable without a network.
 * ---------------------------------------------------------------------- */

int osr_winbin_match(const char *pattern, const char *text) {
    const char *p = pattern;
    const char *t = text;
    const char *star = NULL;
    const char *retry = NULL;

    while (*t != '\0') {
        if (*p == '*') {
            star = p;
            p++;
            retry = t;
        } else if (*p == *t) {
            p++;
            t++;
        } else if (star != NULL) {
            /* backtrack: let the last '*' swallow one more character */
            p = star + 1;
            retry++;
            t = retry;
        } else {
            return 0;
        }
    }

    while (*p == '*') p++;
    return *p == '\0';
}

/* basename_of -- the part of a URL after the last '/'. */
static const char *basename_of(const char *url) {
    const char *slash = strrchr(url, '/');
    return (slash != NULL) ? slash + 1 : url;
}

/* cat_bounded -- append src to dst, or refuse and leave dst untouched when
 * it would not fit. Every path and command below is assembled with this
 * rather than sprintf: the inputs are filesystem paths whose lengths are
 * only known at runtime, and a silently truncated path is a command that
 * acts on the wrong file. Returns 1 when the append happened.
 */
static int cat_bounded(char *dst, unsigned long dst_sz, const char *src) {
    unsigned long dst_len = (unsigned long)strlen(dst);
    unsigned long src_len = (unsigned long)strlen(src);

    if (dst_len + src_len >= dst_sz) return 0;
    memcpy(dst + dst_len, src, src_len + 1);
    return 1;
}

/* ends_with_ci -- case-insensitive suffix test, without depending on a
 * platform's _stricmp in this portable half of the file. */
static int ends_with_ci(const char *text, const char *suffix) {
    unsigned long tlen = (unsigned long)strlen(text);
    unsigned long slen = (unsigned long)strlen(suffix);
    unsigned long i;

    if (slen > tlen) return 0;
    text += tlen - slen;

    for (i = 0; i < slen; i++) {
        char a = text[i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
    }
    return 1;
}

osr_winbin_kind osr_winbin_kind_of_file(const char *filename) {
    if (ends_with_ci(filename, ".zip")) return OSR_WINBIN_KIND_ZIP;
    if (ends_with_ci(filename, ".msi")) return OSR_WINBIN_KIND_MSI;
    /* A bare .exe is assumed to be the program, not an installer: that is
     * what vendors publish alongside archives, and a builder that means
     * otherwise says so with an explicit `setup,` kind (whose silent
     * switches it must supply anyway). */
    if (ends_with_ci(filename, ".exe")) return OSR_WINBIN_KIND_EXE;
    return OSR_WINBIN_KIND_AUTO;
}

int osr_winbin_parse_spec(const char *spec, osr_winbin_kind *kind,
                          char *args, unsigned long args_sz,
                          char *source, unsigned long source_sz) {
    static const struct { const char *name; osr_winbin_kind kind; } kinds[] = {
        { "zip",   OSR_WINBIN_KIND_ZIP },
        { "exe",   OSR_WINBIN_KIND_EXE },
        { "msi",   OSR_WINBIN_KIND_MSI },
        { "setup", OSR_WINBIN_KIND_SETUP }
    };
    const char *colon;
    const char *comma;
    unsigned long head_len;
    unsigned long name_len;
    unsigned long i;

    *kind = OSR_WINBIN_KIND_AUTO;
    args[0] = '\0';
    source[0] = '\0';

    if (spec == NULL || spec[0] == '\0') return 0;

    /* The kind, if present, is the text before the first ':'. A plain URL
     * puts "https" there and a gh source puts "gh", neither of which is a
     * kind -- so an unprefixed spec falls through untouched. */
    colon = strchr(spec, ':');
    if (colon != NULL) {
        head_len = (unsigned long)(colon - spec);
        comma = memchr(spec, ',', head_len);
        name_len = (comma != NULL) ? (unsigned long)(comma - spec) : head_len;

        for (i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
            if (name_len != (unsigned long)strlen(kinds[i].name)) continue;
            if (strncmp(spec, kinds[i].name, name_len) != 0) continue;

            *kind = kinds[i].kind;

            /* `setup,/S,/NORESTART:` -- the switches after the kind become
             * the installer's arguments, one per comma. */
            if (comma != NULL) {
                unsigned long args_len = head_len - name_len - 1;
                if (args_len >= args_sz) return 0;
                memcpy(args, comma + 1, args_len);
                args[args_len] = '\0';
                for (i = 0; i < args_len; i++) {
                    if (args[i] == ',') args[i] = ' ';
                }
            }

            spec = colon + 1;
            break;
        }
    }

    if (spec[0] == '\0' || strlen(spec) >= source_sz) return 0;
    strcpy(source, spec);
    return 1;
}

int osr_winbin_pick_asset(const char *json, const char *pattern,
                          char *out, unsigned long out_sz) {
    static const char key[] = "\"browser_download_url\"";
    const char *p = json;

    out[0] = '\0';

    for (;;) {
        const char *start;
        const char *end;
        unsigned long len;

        p = strstr(p, key);
        if (p == NULL) return 0;
        p += sizeof(key) - 1;

        /* ... "browser_download_url" : "https://..." -- tolerate any
         * spacing between the key, the colon and the value. */
        while (*p == ' ' || *p == '\t' || *p == ':') p++;
        if (*p != '"') continue;

        start = p + 1;
        end = strchr(start, '"');
        if (end == NULL) return 0;

        len = (unsigned long)(end - start);
        if (len < out_sz) {
            memcpy(out, start, len);
            out[len] = '\0';
            /* Matching the filename, never the whole URL, so a pattern can
             * never be satisfied by something in the host or path. */
            if (osr_winbin_match(pattern, basename_of(out))) return 1;
            out[0] = '\0';
        }

        p = end + 1;
    }
}

/* -------------------------------------------------------------------------
 * the toolkit itself -- Windows only.
 * ---------------------------------------------------------------------- */

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "config_copy.h"
#include "elevate.h"
#include "net.h"
#include "winui.h"
#include "winpkg.h"

/* osr_dir -- %LOCALAPPDATA%\osr\<sub>\<name>. Per-user by construction: no
 * Program Files, no HKLM, no admin.
 */
static int osr_dir(const char *sub, const char *name, char *out, unsigned long out_sz) {
    char base[MAX_PATH];

    if (GetEnvironmentVariableA("LOCALAPPDATA", base, (DWORD)sizeof(base)) == 0) {
        if (GetEnvironmentVariableA("USERPROFILE", base, (DWORD)sizeof(base)) == 0) return 0;
        strcat(base, "\\AppData\\Local");
    }

    out[0] = '\0';
    if (!cat_bounded(out, out_sz, base)) return 0;
    if (!cat_bounded(out, out_sz, "\\osr\\")) return 0;
    if (!cat_bounded(out, out_sz, sub)) return 0;
    if (!cat_bounded(out, out_sz, "\\")) return 0;
    if (!cat_bounded(out, out_sz, name)) return 0;
    return 1;
}

int osr_winbin_bin_dir(const char *name, char *out, unsigned long out_sz) {
    return osr_dir("bin", name, out, out_sz);
}

int osr_winbin_src_dir(const char *name, char *out, unsigned long out_sz) {
    return osr_dir("src", name, out, out_sz);
}

int osr_winbin_resolve(const char *source, char *url_out, unsigned long url_sz) {
    char api[300];
    char repo[200];
    const char *pattern;
    const char *sep;
    char *json;
    unsigned long json_len;
    int ok;

    if (strncmp(source, "gh:", 3) != 0) {
        if (strlen(source) >= url_sz) return 0;
        strcpy(url_out, source);
        return 1;
    }

    /* gh:<owner>/<repo>:<pattern> */
    sep = strchr(source + 3, ':');
    if (sep == NULL) {
        osr_warn("source '%s' is missing its asset pattern", source);
        return 0;
    }
    if ((unsigned long)(sep - (source + 3)) >= sizeof(repo)) return 0;
    memcpy(repo, source + 3, (unsigned long)(sep - (source + 3)));
    repo[sep - (source + 3)] = '\0';
    pattern = sep + 1;

    api[0] = '\0';
    if (!cat_bounded(api, sizeof(api), "https://api.github.com/repos/") ||
        !cat_bounded(api, sizeof(api), repo) ||
        !cat_bounded(api, sizeof(api), "/releases/latest")) return 0;

    json = NULL;
    json_len = 0;
    if (osr_fetch_to_buffer(api, &json, &json_len) != OSR_NET_OK || json == NULL) {
        osr_warn("could not reach %s to find the latest release", api);
        return 0;
    }

    ok = osr_winbin_pick_asset(json, pattern, url_out, url_sz);
    free(json);

    if (!ok) osr_warn("no asset matching '%s' in the latest %s release", pattern, repo);
    return ok;
}

int osr_winbin_fetch(const char *url, char *path_out, unsigned long path_sz) {
    char temp_dir[MAX_PATH];
    char filename[MAX_PATH];

    osr_url_filename(url, filename, sizeof(filename));
    if (filename[0] == '\0') {
        osr_warn("nothing to download -- no filename in %s", url);
        return 0;
    }

    if (GetTempPathA((DWORD)sizeof(temp_dir), temp_dir) == 0) return 0;

    path_out[0] = '\0';
    if (!cat_bounded(path_out, path_sz, temp_dir) ||
        !cat_bounded(path_out, path_sz, filename)) return 0;

    if (osr_download(url, path_out) != OSR_NET_OK) {
        osr_warn("download failed: %s", url);
        return 0;
    }
    return 1;
}

int osr_winbin_unzip(const char *archive, const char *dest_dir) {
    char cmd[1600];
    char desc[300];
    int ok;

    /* Expand-Archive when the host has PowerShell 5+, else the
     * Shell.Application COM route that has worked since well before it.
     * Both attempts in one command so a single step covers them. */
    cmd[0] = '\0';
    ok = cat_bounded(cmd, sizeof(cmd),
            "powershell -NoProfile -ExecutionPolicy Bypass -Command "
            "\"$ErrorActionPreference='Stop'; "
            "New-Item -ItemType Directory -Force -Path '")
       && cat_bounded(cmd, sizeof(cmd), dest_dir)
       && cat_bounded(cmd, sizeof(cmd), "' | Out-Null; try { Expand-Archive -LiteralPath '")
       && cat_bounded(cmd, sizeof(cmd), archive)
       && cat_bounded(cmd, sizeof(cmd), "' -DestinationPath '")
       && cat_bounded(cmd, sizeof(cmd), dest_dir)
       && cat_bounded(cmd, sizeof(cmd), "' -Force } catch { $s = New-Object -ComObject "
                                        "Shell.Application; $s.NameSpace('")
       && cat_bounded(cmd, sizeof(cmd), dest_dir)
       && cat_bounded(cmd, sizeof(cmd), "').CopyHere($s.NameSpace('")
       && cat_bounded(cmd, sizeof(cmd), archive)
       && cat_bounded(cmd, sizeof(cmd), "').Items(), 16) }\"");
    if (!ok) return 0;

    desc[0] = '\0';
    cat_bounded(desc, sizeof(desc), "extracting ");
    cat_bounded(desc, sizeof(desc), basename_of(archive));

    return osr_run_step(desc, cmd) == 0;
}

int osr_winbin_find_exe_dir(const char *root, const char *exe_name, int depth,
                            char *out, unsigned long out_sz) {
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char pattern[MAX_PATH];
    char child[MAX_PATH];
    int found;

    if (depth < 0) return 0;
    pattern[0] = '\0';
    if (!cat_bounded(pattern, sizeof(pattern), root) ||
        !cat_bounded(pattern, sizeof(pattern), "\\*")) return 0;

    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;

    found = 0;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;

        child[0] = '\0';
        if (!cat_bounded(child, sizeof(child), root) ||
            !cat_bounded(child, sizeof(child), "\\") ||
            !cat_bounded(child, sizeof(child), fd.cFileName)) continue;

        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if (osr_winbin_find_exe_dir(child, exe_name, depth - 1, out, out_sz)) {
                found = 1;
                break;
            }
        } else if (_stricmp(fd.cFileName, exe_name) == 0) {
            if (strlen(root) < out_sz) { strcpy(out, root); found = 1; }
            break;
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
    return found;
}

int osr_winbin_place(const char *src_file, const char *dest_dir, const char *exe_name) {
    char dest[MAX_PATH];

    dest[0] = '\0';
    if (!cat_bounded(dest, sizeof(dest), dest_dir) ||
        !cat_bounded(dest, sizeof(dest), "\\") ||
        !cat_bounded(dest, sizeof(dest), exe_name)) return 0;

    /* osr_copy_file creates the parent tree, so a builder never has to. */
    if (!osr_copy_file(src_file, dest)) {
        osr_warn("failed to place %s", dest);
        return 0;
    }
    return 1;
}

int osr_winbin_file_exists(const char *path) {
    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

/* path_contains -- is dir already one of the ';'-separated entries in
 * path_value? Case-insensitive, and a trailing backslash does not make an
 * entry different, so a re-run does not append a second copy.
 */
static int path_contains(const char *path_value, const char *dir) {
    const char *p = path_value;
    unsigned long dir_len = (unsigned long)strlen(dir);

    while (*p != '\0') {
        const char *end = strchr(p, ';');
        unsigned long len = (end != NULL) ? (unsigned long)(end - p) : (unsigned long)strlen(p);

        while (len > 0 && p[len - 1] == '\\') len--;
        if (len == dir_len && _strnicmp(p, dir, len) == 0) return 1;

        if (end == NULL) break;
        p = end + 1;
    }
    return 0;
}

void osr_winbin_add_to_path(const char *dir) {
    HKEY key;
    DWORD type;
    DWORD len;
    char current[8192];
    char updated[8192];
    char process_path[16384];

    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0,
                      KEY_READ | KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        current[0] = '\0';
        type = REG_EXPAND_SZ;
        len = (DWORD)sizeof(current) - 1;

        if (RegQueryValueExA(key, "Path", NULL, &type, (BYTE *)current, &len) == ERROR_SUCCESS) {
            if (len >= sizeof(current)) len = (DWORD)sizeof(current) - 1;
            current[len] = '\0';
        } else {
            current[0] = '\0';
            type = REG_EXPAND_SZ;
        }
        if (type != REG_SZ && type != REG_EXPAND_SZ) type = REG_EXPAND_SZ;

        updated[0] = '\0';
        if (!path_contains(current, dir) &&
            cat_bounded(updated, sizeof(updated), current) &&
            (current[0] == '\0' || cat_bounded(updated, sizeof(updated), ";")) &&
            cat_bounded(updated, sizeof(updated), dir)) {

            if (RegSetValueExA(key, "Path", 0, type, (const BYTE *)updated,
                               (DWORD)strlen(updated) + 1) == ERROR_SUCCESS) {
                /* Without this, already-running shells keep the old PATH
                 * until they restart. */
                SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                                    (LPARAM)"Environment", SMTO_ABORTIFHUNG, 1000, NULL);
            }
        }
        RegCloseKey(key);
    }

    if (GetEnvironmentVariableA("Path", process_path, (DWORD)sizeof(process_path)) > 0) {
        if (!path_contains(process_path, dir) &&
            strlen(process_path) + 1 + strlen(dir) < sizeof(process_path)) {
            strcat(process_path, ";");
            strcat(process_path, dir);
            SetEnvironmentVariableA("Path", process_path);
        }
    }
}

int osr_winbin_run_installer(osr_winbin_kind kind, const char *file,
                             const char *args, const char *name) {
    char cmd[900];
    char desc[300];
    int ok;

    if (!osr_is_admin() && !osr_elevate_now("this package installs through a system-wide "
                                            "installer, which needs Administrator rights.")) {
        osr_warn("%s needs Administrator to run its installer -- skipped", name);
        return 0;
    }

    cmd[0] = '\0';
    if (kind == OSR_WINBIN_KIND_MSI) {
        /* /qn silent, /norestart so a package can never reboot the machine
         * out from under a rice that is still running. */
        ok = cat_bounded(cmd, sizeof(cmd), "msiexec /i \"")
          && cat_bounded(cmd, sizeof(cmd), file)
          && cat_bounded(cmd, sizeof(cmd), "\" /qn /norestart");
    } else {
        ok = cat_bounded(cmd, sizeof(cmd), "\"")
          && cat_bounded(cmd, sizeof(cmd), file)
          && cat_bounded(cmd, sizeof(cmd), "\"");
        if (ok && args != NULL && args[0] != '\0') {
            ok = cat_bounded(cmd, sizeof(cmd), " ") && cat_bounded(cmd, sizeof(cmd), args);
        }
    }
    if (!ok) return 0;

    desc[0] = '\0';
    cat_bounded(desc, sizeof(desc), name);
    cat_bounded(desc, sizeof(desc), ": running the vendor installer");

    return osr_run_step(desc, cmd) == 0;
}

int osr_winbin_run_script(const char *url, const char *name) {
    char cmd[900];
    char desc[300];
    int ok;

    /* `irm <url> | iex` is what a vendor means by "paste this line into a
     * shell", and is the same shape lib/pkg.sh's _via_script uses on Linux
     * (fetch to stdout, pipe to the interpreter). */
    cmd[0] = '\0';
    ok = cat_bounded(cmd, sizeof(cmd),
            "powershell -NoProfile -ExecutionPolicy Bypass -Command \"irm ")
      && cat_bounded(cmd, sizeof(cmd), url)
      && cat_bounded(cmd, sizeof(cmd), " | iex\"");
    if (!ok) return 0;

    desc[0] = '\0';
    cat_bounded(desc, sizeof(desc), name);
    cat_bounded(desc, sizeof(desc), ": running the vendor install script");

    if (osr_run_step(desc, cmd) != 0) return 0;
    osr_winpkg_refresh_env();
    return 1;
}

int osr_winbin_artifact(const char *spec, const char *name, const char *test_command) {
    char source[600];
    char args[200];
    char url[600];
    char filename[MAX_PATH];
    char download_path[MAX_PATH];
    char dest_dir[MAX_PATH];
    char exe_dir[MAX_PATH];
    char exe_name[MAX_PATH];
    osr_winbin_kind kind;

    if (!osr_net_available()) {
        osr_warn("no download backend on this build -- cannot fetch %s", name);
        return 0;
    }
    if (!osr_winbin_parse_spec(spec, &kind, args, sizeof(args), source, sizeof(source))) {
        osr_warn("artifact spec for %s is malformed: %s", name, spec);
        return 0;
    }
    if (!osr_winbin_resolve(source, url, sizeof(url))) return 0;
    if (!osr_winbin_bin_dir(name, dest_dir, sizeof(dest_dir))) {
        osr_warn("cannot determine %%LOCALAPPDATA%% -- no place to install %s", name);
        return 0;
    }

    osr_info("%s: fetching %s", name, url);
    if (!osr_winbin_fetch(url, download_path, sizeof(download_path))) return 0;

    osr_url_filename(url, filename, sizeof(filename));

    exe_name[0] = '\0';
    if (!cat_bounded(exe_name, sizeof(exe_name), test_command) ||
        !cat_bounded(exe_name, sizeof(exe_name), ".exe")) return 0;

    /* The spec may state the kind; otherwise the extension of whatever was
     * actually downloaded decides -- which for a gh: source is the only
     * thing that knows, since the asset name is not visible until it
     * resolves. */
    if (kind == OSR_WINBIN_KIND_AUTO) kind = osr_winbin_kind_of_file(filename);
    if (kind == OSR_WINBIN_KIND_AUTO) {
        osr_warn("%s: downloaded '%s', whose kind is not recognised -- name it "
                 "explicitly (zip:/exe:/msi:/setup,<switches>:)", name, filename);
        return 0;
    }

    if (kind == OSR_WINBIN_KIND_MSI || kind == OSR_WINBIN_KIND_SETUP) {
        /* A vendor installer decides its own layout, so there is nothing to
         * place and nothing to add to PATH -- only to re-read the
         * environment it changed and check the command showed up. */
        if (!osr_winbin_run_installer(kind, download_path, args, name)) return 0;

        osr_winpkg_refresh_env();
        if (osr_winpkg_have_command(test_command)) return 1;

        osr_warn("%s: the installer reported success but '%s' is not on PATH yet -- "
                 "open a new shell", name, test_command);
        return 1;
    }

    if (kind == OSR_WINBIN_KIND_ZIP) {
        if (!osr_winbin_unzip(download_path, dest_dir)) {
            osr_warn("could not extract %s", filename);
            return 0;
        }
        if (!osr_winbin_find_exe_dir(dest_dir, exe_name, 3, exe_dir, sizeof(exe_dir))) {
            osr_warn("%s not found anywhere in %s", exe_name, filename);
            return 0;
        }
    } else {
        /* A portable .exe is the program itself, published under a name that
         * describes the build (posh-windows-amd64.exe) rather than the
         * command it provides -- so it is placed under the command's name. */
        if (!osr_winbin_place(download_path, dest_dir, exe_name)) return 0;
        strcpy(exe_dir, dest_dir);
    }

    osr_winbin_add_to_path(exe_dir);

    if (osr_winpkg_have_command(test_command)) return 1;

    osr_warn("%s was placed in %s but '%s' still does not resolve", name, exe_dir, test_command);
    return 0;
}

#else /* !_WIN32 */

/* The toolkit is Windows-only -- lib/build.sh is its opposite number on the
 * Linux side, and the two share no code, only a shape. Stubs keep the C
 * core linking on a Linux host so the pure helpers above stay testable.
 */

int osr_winbin_bin_dir(const char *name, char *out, unsigned long out_sz) {
    (void)name; (void)out; (void)out_sz;
    return 0;
}

int osr_winbin_src_dir(const char *name, char *out, unsigned long out_sz) {
    (void)name; (void)out; (void)out_sz;
    return 0;
}

int osr_winbin_resolve(const char *source, char *url_out, unsigned long url_sz) {
    (void)source; (void)url_out; (void)url_sz;
    return 0;
}

int osr_winbin_fetch(const char *url, char *path_out, unsigned long path_sz) {
    (void)url; (void)path_out; (void)path_sz;
    return 0;
}

int osr_winbin_unzip(const char *archive, const char *dest_dir) {
    (void)archive; (void)dest_dir;
    return 0;
}

int osr_winbin_find_exe_dir(const char *root, const char *exe_name, int depth,
                            char *out, unsigned long out_sz) {
    (void)root; (void)exe_name; (void)depth; (void)out; (void)out_sz;
    return 0;
}

int osr_winbin_place(const char *src_file, const char *dest_dir, const char *exe_name) {
    (void)src_file; (void)dest_dir; (void)exe_name;
    return 0;
}

int osr_winbin_file_exists(const char *path) {
    (void)path;
    return 0;
}

void osr_winbin_add_to_path(const char *dir) {
    (void)dir;
}

int osr_winbin_run_installer(osr_winbin_kind kind, const char *file,
                             const char *args, const char *name) {
    (void)kind; (void)file; (void)args; (void)name;
    return 0;
}

int osr_winbin_run_script(const char *url, const char *name) {
    (void)url; (void)name;
    return 0;
}

int osr_winbin_artifact(const char *spec, const char *name, const char *test_command) {
    (void)spec; (void)name; (void)test_command;
    return 0;
}

#endif /* _WIN32 */
