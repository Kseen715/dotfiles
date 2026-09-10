/* archive_test.c -- lib/archive.c reads every format it claims to, and its
 * write path refuses what an archive should never be allowed to do.
 *
 * Every case forces allow_system = 0, so what runs is the vendored path --
 * the one that exists FOR the boxes that have no tar and no unzip, and the
 * one a build machine with both would otherwise never execute.
 *
 * The fixtures under fixtures/archives/ all carry the same tiny tree (see
 * that directory's make.sh), so one set of assertions covers nine formats.
 * There is no RAR fixture: nothing but WinRAR writes one.
 */
#include "../c_test.h"

#include "../../lib/archive.h"
#include "../../lib/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>   /* symlink(), for the nofollow case */
#endif

/* dest_for -- a fresh directory per case, under the temp directory.
 *
 * ponytail: not deleted afterwards. A recursive delete is thirty lines of
 * #ifdef for two hundred bytes of scratch in a directory the system already
 * cleans; the pid in the name is what keeps two runs apart. */
static const char *dest_for(const char *tag) {
    static char buf[OSR_PATH_MAX];
    char leaf[128];
    sprintf(leaf, "osr-archive-test-%ld-%s", osr_pid(), tag);
    if (!osr_path_join(buf, sizeof(buf), osr_tmpdir(), leaf)) buf[0] = '\0';
    return buf;
}

static int unpack(const char *archive, const char *dest, int strip) {
    OsrExtract o;
    osr_extract_init(&o);
    o.archive = archive;
    o.dest_dir = dest;
    o.strip_components = strip;
    o.quiet = 1;
    o.allow_system = 0;   /* the vendored path is the point of the test */
    return osr_extract(&o);
}

/* file_is -- the file exists and holds exactly this text. */
static int file_is(const char *dir, const char *rel, const char *want) {
    char path[OSR_PATH_MAX];
    char *text;
    size_t len;
    int same;

    if (!osr_path_join(path, sizeof(path), dir, rel)) return 0;
    text = slurp(path, &len);
    if (text == NULL) return 0;
    same = (len == strlen(want) && memcmp(text, want, len) == 0);
    free(text);
    return same;
}

static void case_format(const char *fixture, const char *want) {
    char label[128];
    char path[OSR_PATH_MAX];
    sprintf(path, "fixtures/archives/%s", fixture);
    sprintf(label, "%s sniffs as %s", fixture, want);
    osr_t_eq_str(label, osr_archive_format(path), want);
}

/* case_tree -- one fixture, unpacked, checked against what make.sh put in. */
static void case_tree(const char *fixture, const char *tag) {
    char path[OSR_PATH_MAX];
    char label[160];
    const char *dest = dest_for(tag);

    sprintf(path, "fixtures/archives/%s", fixture);
    if (!unpack(path, dest, 0)) {
        sprintf(label, "%s unpacks", fixture);
        osr_t_fail_msg(label, "osr_extract failed");
        return;
    }
    sprintf(label, "%s unpacks", fixture);
    osr_t_ok(label);

    sprintf(label, "%s: p/hello.txt", fixture);
    if (file_is(dest, "p/hello.txt", "hello\n")) {
        osr_t_ok(label);
    } else {
        osr_t_fail_msg(label, "content differs or missing");
    }

    sprintf(label, "%s: p/sub/deep.txt", fixture);
    if (file_is(dest, "p/sub/deep.txt", "deep\n")) {
        osr_t_ok(label);
    } else {
        osr_t_fail_msg(label, "content differs or missing");
    }

#ifndef _WIN32
    {
        struct stat st;
        char exe[OSR_PATH_MAX];
        sprintf(label, "%s: p/run.sh keeps its execute bit", fixture);
        if (osr_path_join(exe, sizeof(exe), dest, "p/run.sh") &&
            stat(exe, &st) == 0 && (st.st_mode & 0111) != 0) {
            osr_t_ok(label);
        } else {
            osr_t_fail_msg(label, "not executable");
        }
    }
#endif
}

int main(void) {
    const char *dest;

    OSR_T_INIT();

    case_format("plain.tar", "tar");
    case_format("plain.tar.gz", "gzip");
    case_format("plain.tar.bz2", "bzip2");
    case_format("plain.tar.xz", "xz");
    case_format("plain.tar.zst", "zstd");
    case_format("plain.zip", "zip");
    case_format("plain.7z", "7z");
    case_format("plain.a", "ar");
    case_format("make.sh", "");

    case_tree("plain.tar", "tar");
    case_tree("plain.tar.gz", "gz");
    case_tree("plain.tar.bz2", "bz2");
    case_tree("plain.tar.xz", "xz");
    case_tree("plain.tar.zst", "zst");
    case_tree("plain.zip", "zip");
    case_tree("plain.7z", "7z");

    /* strip_components drops the leading directory, which is what every
     * `tar --strip-components=1` call site in lib/build.c wants. */
    dest = dest_for("strip");
    if (unpack("fixtures/archives/plain.tar", dest, 1) &&
        file_is(dest, "hello.txt", "hello\n")) {
        osr_t_ok("strip_components drops the leading component");
    } else {
        osr_t_fail_msg("strip_components drops the leading component", "hello.txt not at the root");
    }

    /* A compressed stream that is not a container is the file itself, named
     * after the archive minus its suffix -- what gunzip does. */
    dest = dest_for("single");
    if (unpack("fixtures/archives/single.gz", dest, 0) &&
        file_is(dest, "single", "hello\n")) {
        osr_t_ok("a bare .gz lands as the file it holds");
    } else {
        osr_t_fail_msg("a bare .gz lands as the file it holds", "single missing or wrong");
    }

    /* The ar container a .deb is. */
    dest = dest_for("ar");
    if (unpack("fixtures/archives/plain.a", dest, 0) &&
        file_is(dest, "debian-binary", "2.0\n")) {
        osr_t_ok("ar members come out whole");
    } else {
        osr_t_fail_msg("ar members come out whole", "debian-binary missing or wrong");
    }

    /* --- the write path, which is a trust boundary ------------------ */

    dest = dest_for("traversal");
    (void)unpack("fixtures/archives/traversal.tar", dest, 0);
    {
        char escaped[OSR_PATH_MAX];
        char parent[OSR_PATH_MAX];
        osr_dirname(dest, parent, sizeof(parent));
        osr_path_join(escaped, sizeof(escaped), parent, "escaped");
        if (file_exists(escaped)) {
            osr_t_fail_msg("a .. member writes nothing outside dest_dir", escaped);
        } else {
            osr_t_ok("a .. member writes nothing outside dest_dir");
        }
        osr_path_join(escaped, sizeof(escaped), parent, "escaped2");
        if (file_exists(escaped)) {
            osr_t_fail_msg("a buried .. member writes nothing outside dest_dir", escaped);
        } else {
            osr_t_ok("a buried .. member writes nothing outside dest_dir");
        }
    }
    if (file_is(dest, "ok.txt", "ok\n")) {
        osr_t_ok("the safe members of that archive still land");
    } else {
        osr_t_fail_msg("the safe members of that archive still land", "ok.txt missing");
    }

    dest = dest_for("absolute");
    (void)unpack("fixtures/archives/absolute.tar", dest, 0);
    if (file_exists("/tmp/osr-escaped")) {
        osr_t_fail_msg("an absolute member is refused", "/tmp/osr-escaped was written");
    } else {
        osr_t_ok("an absolute member is refused");
    }

#ifndef _WIN32
    dest = dest_for("setuid");
    if (unpack("fixtures/archives/setuid.tar", dest, 0)) {
        struct stat st;
        char suid[OSR_PATH_MAX];
        osr_path_join(suid, sizeof(suid), dest, "suid");
        if (stat(suid, &st) == 0 && (st.st_mode & (S_ISUID | S_ISGID)) == 0) {
            osr_t_ok("setuid and setgid bits are dropped");
        } else {
            osr_t_fail_msg("setuid and setgid bits are dropped", "the bit survived");
        }
    } else {
        osr_t_fail_msg("setuid and setgid bits are dropped", "osr_extract failed");
    }

    /* A symlink already sitting where a member lands is replaced, never
     * written through -- an archive that ships a link to /etc/passwd and then
     * a file of the same name is asking for exactly that. */
    {
        char target[OSR_PATH_MAX];
        char link[OSR_PATH_MAX];
        dest = dest_for("nofollow");
        osr_path_join(target, sizeof(target), osr_tmpdir(), "osr-archive-test-target");
        remove(target);
        osr_path_join(link, sizeof(link), dest, "p");
        osr_mkdir_parents(dest);
        osr_path_join(link, sizeof(link), dest, "p/hello.txt");
        {
            char dir[OSR_PATH_MAX];
            osr_dirname(link, dir, sizeof(dir));
            osr_mkdir_parents(dir);
        }
        remove(link);
        if (symlink(target, link) != 0) {
            osr_t_fail_msg("an existing symlink is not followed", "could not place the link");
        } else {
            (void)unpack("fixtures/archives/plain.tar", dest, 0);
            if (file_exists(target)) {
                osr_t_fail_msg("an existing symlink is not followed", "the target was written");
            } else {
                osr_t_ok("an existing symlink is not followed");
            }
        }
    }
#endif

    return osr_t_finish();
}
