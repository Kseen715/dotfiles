"""amalgamate_bearssl.py -- rebuild bearssl.h from an unpacked BearSSL release.

    curl -L -o bearssl.tar.gz https://bearssl.org/bearssl-0.6.tar.gz
    tar xzf bearssl.tar.gz
    python3 amalgamate_bearssl.py bearssl-0.6 bearssl.h

Concatenates the twelve public headers and the 277 source files into one
single-header library in the style of thirdparty/yaml.h: declarations by
default, code behind BEARSSL_IMPLEMENTATION. The edits it makes to upstream
are listed in the header comment it writes -- keep them here rather than in
bearssl.h, so bumping the vendored version stays a re-run of this script
instead of a merge. Bump the version in PROLOGUE when you do.
"""

PROLOGUE = """/*
 * bearssl.h -- BearSSL 0.6, amalgamated into one single-header library for
 * os-rice.
 *
 * Upstream (https://bearssl.org/, MIT) is a small TLS implementation in
 * plain C with no dependencies -- no libc allocation of its own, no OS
 * calls, no threads: the caller hands it buffers and a socket. os-rice
 * carries it for one reason, spelled out in lib/tls.c: Windows XP's
 * schannel tops out at TLS 1.0, so a binary that wants to fetch anything
 * over HTTPS from a current host has to bring its own TLS stack. On
 * everything newer the system stack (WinINet, or curl/wget on POSIX) is
 * still what runs; this code is the fallback that makes the XP tier work at
 * all.
 *
 * Vendored the way thirdparty/yaml.h is: one file in the source tree, no
 * submodule, no package to install. Same stb-style split -- including it
 * plainly gets the declarations, and exactly one translation unit defines
 * the code (lib/bearssl.c):
 *
 *   #define BEARSSL_IMPLEMENTATION
 *   #include "../thirdparty/bearssl.h"
 *
 * Everywhere else just #include it. The implementation needs C99 (upstream
 * uses `static inline` and declarations after statements) and lib/bearssl.c
 * is the only unit nob.c builds that way; the declarations above it are
 * plain C89, because this file's `static inline` accessors were rewritten to
 * `static` when it was generated. Including bearssl.h therefore costs a unit
 * nothing -- lib/tls.c and the rest of the tree stay at -std=c89.
 *
 * A client handshake, in short (see https://bearssl.org/api1.html and
 * lib/tls.c for the whole thing):
 *
 *   br_ssl_client_context sc;
 *   br_x509_minimal_context xc;
 *   unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
 *   br_ssl_client_init_full(&sc, &xc, anchors, anchor_count);
 *   br_ssl_engine_set_buffer(&sc.eng, iobuf, sizeof iobuf, 1);
 *   br_ssl_client_reset(&sc, host, 0);
 *   br_sslio_init(&ioc, &sc.eng, sock_read, &fd, sock_write, &fd);
 *
 * Regenerating (upstream bugfix, or a newer release -- keep this file's
 * local edits limited to what the script below does, so it stays a pure
 * re-run rather than a merge):
 *
 *   curl -L -o bearssl.tar.gz https://bearssl.org/bearssl-0.6.tar.gz
 *   tar xzf bearssl.tar.gz
 *   python3 amalgamate_bearssl.py bearssl-0.6 bearssl.h
 *
 * where amalgamate_bearssl.py concatenates inc/bearssl.h with the twelve
 * headers it includes inlined in dependency order, then src/inner.h and each
 * .c file under src/ in sorted order, wrapping the sources in
 * `#ifdef BEARSSL_IMPLEMENTATION`; dropping every `#include` of a bearssl
 * header, of inner.h, and of config.h; stripping the repeated upstream
 * copyright block from each file, since one copy of it (below) covers the
 * whole amalgamation; and rewriting the public headers' `static inline`
 * accessors to `static`, which C89 units can include and which costs
 * nothing at -O2, where a compiler inlines a one-line static anyway. src/config.h is not carried at all: every macro in it
 * is commented out upstream, and its only role is to override the
 * autodetection in inner.h, which is what this tree wants running.
 *
 * ---- upstream LICENSE (MIT) ----
 *
 * Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
"""

import os
import re
import sys

src = sys.argv[1]           # unpacked bearssl-0.6 dir
out = sys.argv[2]


def read(p):
    return open(os.path.join(src, p), encoding='utf-8').read()


drop = re.compile(r'^\s*#\s*include\s*[<"](bearssl[a-z0-9_]*\.h|inner\.h|config\.h)[>"]\s*$')
copyright_block = re.compile(r'\A\s*/\*.*?Copyright.*?\*/\s*', re.S)


def clean(text):
    """Strip the repeated upstream copyright header and internal #includes."""
    text = copyright_block.sub('', text)
    return '\n'.join(ln for ln in text.split('\n') if not drop.match(ln))


# The public headers, ordered so that each comes after the ones it uses:
# upstream's inc/bearssl.h includes them in an order that only works because
# each header also includes its own dependencies, and those includes are the
# ones this script removes. Sorting them here rather than hardcoding a list
# means a release that adds or rearranges a header still amalgamates.
HEADERS = sorted(h for h in os.listdir(os.path.join(src, 'inc'))
                 if h.startswith('bearssl_') and h.endswith('.h'))
DEP = re.compile(r'^\s*#\s*include\s*"(bearssl[a-z0-9_]*\.h)"', re.M)


def header_order():
    deps = {h: DEP.findall(read('inc/' + h)) for h in HEADERS}
    order, seen = [], set()

    def visit(h, stack=()):
        assert h not in stack, 'include cycle: %s' % ' -> '.join(stack + (h,))
        if h in seen:
            return
        seen.add(h)
        for d in deps.get(h, ()):
            visit(d, stack + (h,))
        order.append(h)

    for h in HEADERS:
        visit(h)
    return order


HEADERS = header_order()

umbrella = read('inc/bearssl.h')
marker = '#include "bearssl_hash.h"'
assert marker in umbrella, 'inc/bearssl.h no longer includes bearssl_hash.h'
inlined = '\n\n'.join('/* ==== inc/%s ==== */\n%s' % (h, clean(read('inc/' + h)).strip())
                      for h in HEADERS)
# Put the twelve bodies exactly where the umbrella's include block was, so
# upstream's order is preserved: bearssl_hmac.h uses types from
# bearssl_hash.h, bearssl_ssl.h uses most of the rest.
umbrella = clean(umbrella.replace(marker, '@@BEARSSL_HEADERS@@', 1))
assert '@@BEARSSL_HEADERS@@' in umbrella
umbrella = umbrella.replace('@@BEARSSL_HEADERS@@', inlined)

# C89 has no `inline`, and the public headers' accessors are the only thing
# that would force every unit including bearssl.h up to C99 (the sources
# below need C99 regardless, and get it). Dropping the keyword leaves them
# ordinary static functions -- same semantics, and gcc/clang inline a
# one-liner at -O2 with or without the hint.
umbrella = re.sub(r'^static\s+inline\b', 'static', umbrella, flags=re.M)
assert 'static inline' not in umbrella

sources = []
for root, _dirs, files in os.walk(os.path.join(src, 'src')):
    for f in files:
        if f.endswith('.c'):
            sources.append(os.path.relpath(os.path.join(root, f), src))
sources.sort()
assert sources, 'no sources under %s/src' % src

# A handful of sources set a configuration macro before including inner.h --
# aes_x86ni.c's `#define BR_ENABLE_INTRINSICS 1` is what makes inner.h define
# BR_TARGETS_X86_UP and friends. Here inner.h is included once at the top, so
# those defines have to be hoisted above it or the sources that depend on them
# see an inner.h that was expanded without them. Only macros inner.h actually
# reads are moved; a file's private ones (the T0_* byte-encoders) stay where
# they are, since they are needed no earlier than their own body.
INNER_TEXT = read('src/inner.h')
DEFINE_LINE = re.compile(r'^\s*#\s*define\s+([A-Za-z_]\w*)')


def hoisted_defines():
    found = {}
    for f in sources:
        for ln in read(f).split('\n'):
            if re.match(r'^\s*#\s*include\s*"inner\.h"', ln):
                break
            m = DEFINE_LINE.match(ln)
            if m and re.search(r'\b%s\b' % m.group(1), INNER_TEXT):
                prev = found.setdefault(m.group(1), ln.strip())
                assert prev == ln.strip(), \
                    '%s: conflicting definition of %s' % (f, m.group(1))
    return found


HOISTED = hoisted_defines()
hoist_line = re.compile(r'^\s*#\s*define\s+(%s)\b' % '|'.join(HOISTED) if HOISTED else r'(?!)')

parts = [PROLOGUE, umbrella.rstrip(), '',
         '#ifdef BEARSSL_IMPLEMENTATION', '',
         '/* configuration the sources below set before including inner.h */']
parts += [HOISTED[k] for k in sorted(HOISTED)]
parts += ['',
          '/* ==== src/inner.h ==== */',
          clean(read('src/inner.h')).strip(), '']

# Upstream compiles one object per source file, so two files are free to
# define a file-scope `static` of the same name -- and 70-odd of them do
# (every ec_*.c has an api_mul, every T0-generated file a t0_codeblock).
# Concatenated into one translation unit those become redefinitions, so each
# file's colliding names are renamed to a per-file spelling around its body.
# Only names a file declares static (or as a file-local typedef) are touched,
# and only when a second file declares the same one, so nothing with external
# linkage -- the public API, everything inner.h publishes -- can be caught by
# it. The renames are #defines around one file's body, so the asserts below
# rule out the two spellings a #define would also rewrite by mistake.
STATIC_DECL = re.compile(r'^static\b(?:[^;{=]|\n)*?([A-Za-z_]\w*)\s*[\(\[=;]', re.M)
# File-local typedefs collide the same way (two ec_*.c files each define
# their own `p256_jacobian`), and a #define renames a type name as readily as
# a function name.
TYPEDEF_DECL = re.compile(r'^\}\s*([A-Za-z_]\w*)\s*;|^typedef\s+[^;{]*?([A-Za-z_]\w*)\s*;', re.M)


def declared_names(text):
    names = set(STATIC_DECL.findall(text))
    for a, b in TYPEDEF_DECL.findall(text):
        names.add(a or b)
    return names


def stem_of(path):
    return re.sub(r'\W', '_', os.path.splitext(os.path.basename(path))[0])


def collisions(bodies):
    """Names more than one file declares file-locally, per file."""
    declared = {f: declared_names(t) for f, t in bodies.items()}
    seen, dup = {}, set()
    for f, names in declared.items():
        for name in names:
            if name in seen and seen[name] != f:
                dup.add(name)
            seen.setdefault(name, f)
    per_file = {}
    for f, names in declared.items():
        local = sorted(names & dup)
        for name in local:
            # A rename is a #define, so within the file it would also rewrite
            # a struct field or a type tag of the same name. Upstream has no
            # such overlap in a file that declares the name; fail loudly
            # rather than silently miscompile if a release grows one.
            assert not re.search(r'(?:\.|->)\s*' + name + r'\b', bodies[f]), \
                'cannot rename %s: used as a member in %s' % (name, f)
            assert not re.search(r'\b(?:struct|union|enum)\s+' + name + r'\b', bodies[f]), \
                'cannot rename %s: used as a tag in %s' % (name, f)
        per_file[f] = local
    return per_file


bodies = {f: '\n'.join(ln for ln in clean(read(f)).split('\n')
                       if not hoist_line.match(ln))
          for f in sources}
RENAMES = collisions(bodies)

# Same story for the preprocessor: a macro defined in one file dies at the
# end of its translation unit upstream, but would leak into every later file
# here -- ec_c25519_i15.c's `#define f255_mul(d, a, b)` collides with the
# function ec_c25519_i31.c defines under that name. Undefining each file's
# own macros after its body restores the per-file lifetime.
MACRO_DEF = re.compile(r'^\s*#\s*define\s+([A-Za-z_]\w*)', re.M)

for f in sources:
    stem = stem_of(f)
    local = RENAMES[f]
    parts.append('/* ==== %s ==== */' % f)
    for n in local:
        parts.append('#define %s br_amalg_%s_%s' % (n, stem, n))
    parts.append(bodies[f].strip())
    for n in local:
        parts.append('#undef %s' % n)
    for m in sorted(set(MACRO_DEF.findall(bodies[f]))):
        parts.append('#undef %s' % m)
    parts.append('')

parts.append('#endif /* BEARSSL_IMPLEMENTATION */')
open(out, 'w', encoding='utf-8').write('\n'.join(parts) + '\n')
print('%s: %d headers, %d sources, %d renamed statics'
      % (out, len(HEADERS) + 1, len(sources), len(set(n for v in RENAMES.values() for n in v))))
