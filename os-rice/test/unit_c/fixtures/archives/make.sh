#!/bin/sh
# make.sh -- regenerate the archive fixtures.
#
# The fixtures are checked in because a build box is not required to have the
# tools that write them; this script is how they were made and how a new one
# is added. Run it from this directory. Needs tar, xz, bzip2, zstd, zip, 7z
# and ar -- all only to WRITE, which is what os-rice never does.
#
# The payload is deliberately tiny (a few hundred bytes per archive) and the
# same in every format, so one set of assertions covers them all:
#
#   p/hello.txt   "hello\n", mode 644
#   p/run.sh      "#!/bin/sh\nexit 0\n", mode 755
#   p/sub/deep.txt "deep\n"
#   p/link        symlink -> hello.txt
#
# There is no .rar fixture: nothing writes RAR but WinRAR, which is neither
# free nor on a build box. thirdparty/rar.h's readers were checked against a
# corpus of real archives instead (thirdparty/VENDOR.md says how).
set -e
cd "$(dirname "$0")"
rm -rf src p
mkdir -p src/p/sub
printf 'hello\n' > src/p/hello.txt
printf '#!/bin/sh\nexit 0\n' > src/p/run.sh
chmod 755 src/p/run.sh
printf 'deep\n' > src/p/sub/deep.txt
ln -s hello.txt src/p/link

( cd src && tar -b 1 -cf ../plain.tar p )
( cd src && tar czf ../plain.tar.gz p )
( cd src && tar cjf ../plain.tar.bz2 p )
( cd src && tar cJf ../plain.tar.xz p )
( cd src && tar --zstd -cf ../plain.tar.zst p )
( cd src && zip -qry ../plain.zip p )
rm -f plain.7z && ( cd src && 7z a -snl -bso0 ../plain.7z p >/dev/null )
printf 'hello\n' | gzip -c > single.gz

# the ar container a .deb is: three members, short names
printf '2.0\n' > debian-binary
rm -f plain.a && ar rc plain.a debian-binary plain.tar.gz
rm -f debian-binary

# the write path's fixtures: names that try to leave dest_dir, and a member
# that asks for setuid.
python3 - <<'PY'
import tarfile, io
def add(t, name, data=b'pwned\n', mode=0o644):
    ti = tarfile.TarInfo(name); ti.size = len(data); ti.mode = mode
    t.addfile(ti, io.BytesIO(data))
t = tarfile.open('traversal.tar', 'w')
add(t, '../escaped'); add(t, 'a/../../escaped2'); add(t, 'ok.txt', b'ok\n')
t.close()
t = tarfile.open('absolute.tar', 'w')
add(t, '/tmp/osr-escaped'); add(t, 'ok.txt', b'ok\n')
t.close()
t = tarfile.open('setuid.tar', 'w')
add(t, 'suid', b'x\n', 0o4755)
t.close()
PY
rm -rf src
ls -l
