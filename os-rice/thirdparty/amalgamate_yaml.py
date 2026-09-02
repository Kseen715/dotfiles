"""amalgamate_yaml.py -- rebuild yaml.h from an unpacked LibYAML release.

    curl -L -o yaml.tar.gz \
      https://github.com/yaml/libyaml/releases/download/0.2.5/yaml-0.2.5.tar.gz
    tar xzf yaml.tar.gz
    python3 amalgamate_yaml.py yaml-0.2.5 yaml.h

Concatenates the public header and the eight source files into one C89
single-header library in the style of nob.h: declarations by default, code
behind YAML_IMPLEMENTATION. The edits it makes to upstream are listed in the
header comment it writes -- keep them here rather than in yaml.h, so bumping
the vendored version stays a re-run of this script instead of a merge. Bump
the version in PROLOGUE and in the YAML_VERSION_* defines below when you do.
"""

PROLOGUE = """/*
 * yaml.h -- LibYAML 0.2.5, amalgamated into one C89 header for os-rice.
 *
 * Upstream (https://github.com/yaml/libyaml, MIT) is the reference YAML 1.1
 * implementation: the complete language, anchors and aliases included, plus
 * tags, multi-document streams, block and flow styles, and an emitter. It is
 * plain ANSI C with no dependencies beyond <stdlib.h>/<stdio.h>/<string.h>,
 * so it builds under -std=c89 -pedantic like the rest of this tree.
 *
 * Vendored the way nob.h is: one file in the source tree, no submodule, no
 * package to install. Same stb-style split -- including it plainly gets the
 * declarations, and exactly one translation unit defines the code:
 *
 *   #define YAML_IMPLEMENTATION
 *   #include "yaml.h"
 *
 * Everywhere else just #include "yaml.h". YAML_DECLARE_STATIC is forced on,
 * because a header dropped into the build is always statically linked -- the
 * upstream default would resolve to __declspec(dllimport) on MSVC.
 *
 * Parsing a file, in short (see the upstream docs for the full API):
 *
 *   yaml_parser_t p; yaml_document_t doc;
 *   yaml_parser_initialize(&p);
 *   yaml_parser_set_input_file(&p, fp);
 *   if (yaml_parser_load(&p, &doc)) { ... yaml_document_delete(&doc); }
 *   yaml_parser_delete(&p);
 *
 * yaml_parser_load() resolves aliases for you: an aliased node is the same
 * node id as its anchor, so `*ref` and `&ref` share one yaml_node_t.
 *
 * Regenerating (upstream bugfix, or a newer release -- keep this file's
 * local edits limited to what the script below does, so it stays a pure
 * re-run rather than a merge):
 *
 *   curl -L -o yaml.tar.gz \\
 *     https://github.com/yaml/libyaml/releases/download/0.2.5/yaml-0.2.5.tar.gz
 *   tar xzf yaml.tar.gz
 *   python3 amalgamate_yaml.py yaml-0.2.5 yaml.h
 *
 * where amalgamate_yaml.py concatenates include/yaml.h, then src/yaml_private.h
 * and src/{api,reader,scanner,parser,loader,writer,emitter,dumper}.c in that
 * order, dropping every `#include` of yaml.h/yaml_private.h/config.h (and the
 * `#if HAVE_CONFIG_H` block around the last one), wrapping the sources in
 * `#ifdef YAML_IMPLEMENTATION`, hardcoding the YAML_VERSION_* defines that
 * autoconf would have written into config.h, forcing YAML_DECLARE_STATIC, and\n * replacing the one strdup() call in yaml_strdup() with malloc+memcpy (strdup\n * is POSIX, not ANSI C, so -std=c89 -pedantic leaves it undeclared and the\n * returned pointer would be truncated to int), and bracketing the sources in\n * a GCC diagnostic push/pop that silences the -Wunused-value upstream's PUT()\n * macros raise under this tree's -Wall -Wextra.
 *
 * ---- upstream LICENSE (MIT) ----
 *
 * Copyright (c) 2017-2020 Ingy döt Net
 * Copyright (c) 2006-2016 Kirill Simonov
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
"""
import re, sys, os
src = sys.argv[1]           # unpacked yaml-0.2.5 dir
out = sys.argv[2]

def read(p):
    return open(os.path.join(src, p), encoding='utf-8').read()

drop = re.compile(r'^\s*#\s*include\s*[<"](yaml\.h|yaml_private\.h|config\.h)[>"]\s*$')
def strip_includes(text):
    lines = []
    skip_cfg = 0
    for ln in text.split('\n'):
        if ln.strip() == '#if HAVE_CONFIG_H':
            skip_cfg = 1; continue
        if skip_cfg and ln.strip() == '#endif':
            skip_cfg = 0; continue
        if skip_cfg and drop.match(ln):
            continue
        if drop.match(ln):
            continue
        lines.append(ln)
    return '\n'.join(lines)

pub = read('include/yaml.h')
# single-header vendoring is always a static link; never dllimport
pub = pub.replace('#ifndef YAML_H\n#define YAML_H\n',
                  '#ifndef YAML_H\n#define YAML_H\n\n'
                  '#ifndef YAML_DECLARE_STATIC\n#define YAML_DECLARE_STATIC 1\n#endif\n')

parts = [PROLOGUE, pub.rstrip(), '', '#ifdef YAML_IMPLEMENTATION', '',
         '/* Upstream writes its PUT()/PUT_BREAK() macros as comma expressions whose',
         ' * result is discarded, which -Wall -Wextra (this tree builds with both)',
         ' * reports as -Wunused-value. It is upstream style, not a defect, and the',
         ' * warnings would otherwise land on every build of every unit including this',
         ' * header. */',
         '#if defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6))',
         '#pragma GCC diagnostic push',
         '#pragma GCC diagnostic ignored "-Wunused-value"',
         '#endif',
         '',
         '#ifndef YAML_VERSION_STRING',
         '#define YAML_VERSION_MAJOR 0',
         '#define YAML_VERSION_MINOR 2',
         '#define YAML_VERSION_PATCH 5',
         '#define YAML_VERSION_STRING "0.2.5"',
         '#endif', '']

# strdup() is POSIX, not ANSI C: under -std=c89 -pedantic it is not declared,
# so the call would be an implicit int-returning function and the returned
# pointer would be truncated on LP64. Same allocation, spelled portably.
STRDUP_OLD = '    return (yaml_char_t *)strdup((char *)str);'
STRDUP_NEW = """    {
        size_t size = strlen((char *)str) + 1;
        yaml_char_t *copy = (yaml_char_t *)yaml_malloc(size);
        if (!copy)
            return NULL;
        memcpy(copy, str, size);
        return copy;
    }"""

for f in ['src/yaml_private.h', 'src/api.c', 'src/reader.c', 'src/scanner.c',
          'src/parser.c', 'src/loader.c', 'src/writer.c', 'src/emitter.c',
          'src/dumper.c']:
    parts.append('/* ==== %s ==== */' % os.path.basename(f))
    parts.append(strip_includes(read(f)).replace(STRDUP_OLD, STRDUP_NEW).rstrip())
    parts.append('')

parts.append('#if defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6))')
parts.append('#pragma GCC diagnostic pop')
parts.append('#endif')
parts.append('')
parts.append('#endif /* YAML_IMPLEMENTATION */')
open(out, 'w', encoding='utf-8').write('\n'.join(parts) + '\n')
