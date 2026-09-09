"""amalgamate_lib.py -- the part every amalgamate_*.py in this directory does.

Four vendored decoders arrived at once (LZMA SDK, miniz, bzip2, libarchive's
RAR readers), and the mechanics of folding a multi-file C library into one
stb-style header are the same for all of them: expand the local #includes
once each, rewrite the handful of spellings C89 has no
word for, and keep every file's statics and macros from leaking into the file
after it. Only the file lists, the macro name and the prologue differ, so
those stay in the per-library scripts and the machinery lives here.

thirdparty/amalgamate_bearssl.py predates this module and is left alone: it
carries BearSSL-specific surgery (hoisting the config macros its sources set
before including inner.h) that has no counterpart in the four below, and
rewriting it to fit a shared shape would make a working script a merge.

The output has the same two halves as thirdparty/yaml.h and
thirdparty/bearssl.h -- declarations when you just include it, code in the
one translation unit that defines <MACRO>_IMPLEMENTATION.
"""

import os
import re

# ---------------------------------------------------------------------------
# include expansion
# ---------------------------------------------------------------------------

# The trailing group is the `/* why */` note upstreams like zstd put after the
# include; it has to be consumed here or the line does not look like an
# include at all and the dependency is silently left unexpanded.
LOCAL_INCLUDE = re.compile(
    r'^[ \t]*#[ \t]*include[ \t]*"([^"]+)"[ \t]*(?:/\*.*?\*/|//[^\n]*)?[ \t]*$',
    re.M)


class Expander(object):
    """Inline `#include "local.h"` the way the preprocessor would.

    Upstream's headers carry their own include guards, so a plain
    concatenation would need the file list in dependency order and would
    break the moment a release rearranged one. Expanding instead keeps the
    ordering rule where upstream already stated it -- in the #include lines
    -- and the seen-set does what the guards do: a header's body lands once,
    at the point it is first needed.

    A header in `repeat` is the exception: it carries no include guard of
    its own and hands out a different piece of itself each time, chosen by
    macros the includer sets first (zstd_deps.h is the one). Landing it once
    means landing whichever piece the first includer asked for, in whatever
    #if that includer happened to be inside.

    System includes are left exactly where upstream put them. Hoisting them
    to the top looks tidier and is wrong: half of them sit inside a platform
    branch (`#ifdef _WIN32` around <windows.h>), and a hoisted copy is that
    branch taken unconditionally. Repeating <string.h> across twenty files
    costs nothing -- that is what its include guard is for.
    """

    def __init__(self, roots, skip=(), repeat=()):
        self.roots = list(roots)
        self.skip = set(skip)      # local headers to drop rather than expand
        self.repeat = set(repeat)  # local headers expanded at every include
        self.seen = set()          # resolved paths already emitted

    def find(self, name, relative_to):
        for base in [os.path.dirname(relative_to)] + self.roots:
            p = os.path.normpath(os.path.join(base, name))
            if os.path.isfile(p):
                return p
        return None

    def expand(self, path, clean=None):
        """Return `path` with its local includes expanded, once each."""
        path = os.path.normpath(path)
        if os.path.basename(path) not in self.repeat:
            if path in self.seen:
                return ''
            self.seen.add(path)
        text = read_text(path)
        if clean is not None:
            text = clean(text, path)

        def swap(m):
            name = m.group(1)
            if os.path.basename(name) in self.skip:
                return ''
            target = self.find(name, path)
            if target is None:
                # Not ours to resolve -- leave it for the compiler to fail on
                # loudly rather than silently dropping a dependency.
                return m.group(0)
            body = self.expand(target, clean)
            if not body.strip():
                return ''
            return '/* ==== %s ==== */\n%s' % (name, body.strip())

        return LOCAL_INCLUDE.sub(swap, text)


# ---------------------------------------------------------------------------
# C89 rewrites
# ---------------------------------------------------------------------------

LINE_COMMENT = re.compile(r'''
    (                                   # 1: things a // may hide inside
        "(?:\\.|[^"\\])*"               #    string literal
      | '(?:\\.|[^'\\])*'               #    character constant
      | /\*.*?\*/                       #    block comment
    )
  | //([^\n]*)                          # 2: the line comment itself
''', re.S | re.X)


def rewrite_line_comments(text):
    """`// ...` -> `/* ... */`, the one non-C90 spelling all four upstreams use.

    Every one of these libraries is otherwise C90 (checked with
    `gcc -std=c89 -pedantic` on the exact file sets the scripts take), so this
    is the whole of the dialect work. Doing it with a regex is only safe
    because the alternation above consumes string literals, character
    constants and block comments first -- a bare `s/\\/\\//` would eat the
    `//` in a URL inside a comment banner, and worse, inside a string.

    An embedded `*/` in the comment text would close the replacement early, so
    those are spelled `*\\/`; upstream has none today, but a release that grows
    one should not miscompile silently.
    """
    def swap(m):
        if m.group(1) is not None:
            return m.group(1)
        return '/*%s */' % m.group(2).replace('*/', '*\\/')
    return LINE_COMMENT.sub(swap, text)


def has_line_comment(text):
    """True if a real `//` comment is left -- ignoring the ones that are not.

    A `//` inside a string literal or inside a block comment (`https://...`
    in a banner, which every one of these libraries has) is not a line
    comment and must survive untouched, so the check has to tokenise the same
    way the rewrite does rather than search for the two characters.
    """
    return any(m.group(2) is not None for m in LINE_COMMENT.finditer(text))


BARE_INLINE = re.compile(r'(?<![\w$])inline\b')


def has_bare_inline(text):
    """True if the `inline` keyword survives in code (not in a directive).

    C90 has no `inline`, so a compiler in -std=c89 mode reads it as an
    identifier and the declaration after it stops parsing -- an error whose
    message names the *next* token, which is a long way from the cause. The
    check runs on the finished header so a release that grows a new one says
    so at generation time instead. `__inline` / `__inline__` are extensions
    the compilers do accept and are left alone.
    """
    text = strip_comments_and_strings(text)
    # Preprocessor lines are left out. Every one of these upstreams spells
    # its force-inline macro four ways behind #if/#elif and ends with a
    # plain-`inline` fallback for a compiler nobody here uses (and xxhash
    # #defines `inline` itself around <arm_neon.h>); the branch is dead, but
    # nothing textual can see that. What this catches is `inline` in code,
    # which is always live. A live macro fallback is caught by the thing that
    # catches everything else: compiling the generated header at -std=c89.
    text = re.sub(r'^[ \t]*#.*$', '', text, flags=re.M)
    return BARE_INLINE.search(text) is not None


def strip_comments_and_strings(text):
    return LINE_COMMENT.sub(lambda m: '' if m.group(1) is not None else '', text)


STATIC_INLINE = re.compile(r'^(\s*(?:#\s*define\s+\w+\s+)?static)\s+inline\b', re.M)


def rewrite_static_inline(text):
    """`static inline` -> `static`, which is what C90 can say.

    Every one of these upstreams picks a compiler-specific spelling first
    (`__inline__`, `__forceinline`, `__attribute__((always_inline))`) and
    falls back to plain `static inline` for a compiler it does not recognise
    -- a branch gcc, clang and MSVC never take, but one this amalgamation
    still has to parse. Dropping the keyword loses an optimiser hint in a
    branch nothing compiles. Same rewrite amalgamate_bearssl.py makes.
    """
    return STATIC_INLINE.sub(r'\1', text)


def strip_pragma_once(text):
    return re.sub(r'^[ \t]*#[ \t]*pragma[ \t]+once[ \t]*\r?\n', '', text, flags=re.M)


# ---------------------------------------------------------------------------
# per-file scope: statics and macros
# ---------------------------------------------------------------------------

STATIC_DECL = re.compile(r'^static\b[^;{=]*?([A-Za-z_]\w*)\s*[\(\[=;]', re.M)
MACRO_DEF = re.compile(r'^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)', re.M)
GLOBAL_DEF = re.compile(
    r'^(?!static\b)(?!typedef\b)[A-Za-z_][A-Za-z0-9_ \t*]*?\b([A-Za-z_]\w*)\s*\(', re.M)


def static_collisions(bodies):
    """Names that more than one source declares `static`, per source.

    Upstream compiles one object per .c, so two files may each own a static
    of the same name -- concatenated they are a redefinition. Returns
    {path: [names to rename]}; the caller wraps each body in #define/#undef
    pairs. Same trick as amalgamate_bearssl.py, and the same guard: a rename
    is a macro, so it would also rewrite a struct member or a tag spelled the
    same way. Fail loudly instead of miscompiling.

    A static also collides with an *external* function of the same name in
    another file, which separate translation units hide just as well: the
    LZMA SDK has a public SzAlloc in 7zAlloc.c and a private one in Alloc.c.
    The static is the one that moves, since the external name is API.
    """
    declared = dict((f, set(STATIC_DECL.findall(t))) for f, t in bodies.items())
    external = dict((f, set(GLOBAL_DEF.findall(t))) for f, t in bodies.items())
    owner, dup = {}, set()
    for f in sorted(declared):
        for name in declared[f]:
            if owner.get(name, f) != f:
                dup.add(name)
            owner.setdefault(name, f)
    for f in sorted(declared):
        for name in declared[f]:
            if any(name in external[g] for g in external if g != f):
                dup.add(name)

    per_file = {}
    for f in sorted(declared):
        local = sorted(declared[f] & dup)
        for name in local:
            assert not re.search(r'(?:\.|->)\s*' + name + r'\b', bodies[f]), \
                'cannot rename %s: used as a struct member in %s' % (name, f)
            assert not re.search(r'\b(?:struct|union|enum)\s+' + name + r'\b', bodies[f]), \
                'cannot rename %s: used as a tag in %s' % (name, f)
        per_file[f] = local
    return per_file


def stem_of(path):
    return re.sub(r'\W', '_', os.path.splitext(os.path.basename(path))[0])


# ---------------------------------------------------------------------------
# assembly
# ---------------------------------------------------------------------------

def build(prologue, macro, headers, sources, roots, skip=(), repeat=(),
          clean=None, prefix='amalg', extra_decls='', extra_impl=''):
    """Fold one upstream library into a single-header string.

    prologue    the /* ... */ banner, written by the calling script
    macro       <macro>_IMPLEMENTATION gates the second half
    headers     public headers, expanded first so the declarations half has them
    sources     .c files, expanded into the implementation half
    roots       include search path, in order
    skip        basenames of local headers to drop rather than expand
    repeat      basenames of local headers to expand at every #include
    clean       optional (text, path) -> text, run before anything else
    prefix      symbol prefix for renamed colliding statics
    """
    def prepare(text, path):
        text = strip_pragma_once(text)
        text = rewrite_line_comments(text)
        text = rewrite_static_inline(text)
        if clean is not None:
            text = clean(text, path)
        return text

    ex = Expander(roots, skip, repeat)

    head_parts = []
    for h in headers:
        body = ex.expand(h, prepare).strip()
        if body:
            head_parts.append('/* ==== %s ==== */\n%s' % (rel(h, roots), body))

    bodies, own, order = {}, {}, []
    for s in sources:
        body = ex.expand(s, prepare).strip()
        # A source whose whole content was headers already pulled in still
        # needs its slot, but an empty one adds nothing.
        if body:
            bodies[s] = body
            # What the file itself declares, with the headers expanded into it
            # left out. The per-file #undef and rename wrappers below must see
            # only this: a macro or a static that arrived through an inlined
            # header lands in the output exactly once (the seen-set sees to
            # that), so it can neither collide nor need its scope restored --
            # and undefining it here would take it away from every later file,
            # which is how 7zTypes.h's UNUSED_VAR went missing.
            own[s] = LOCAL_INCLUDE.sub('', prepare(read_text(s), s))
            order.append(s)

    renames = static_collisions(own)

    impl_parts = []
    for s in order:
        stem, local = stem_of(s), renames[s]
        impl_parts.append('/* ==== %s ==== */' % rel(s, roots))
        impl_parts += ['#define %s %s_%s_%s' % (n, prefix, stem, n) for n in local]
        impl_parts.append(bodies[s])
        impl_parts += ['#undef %s' % n for n in local]
        # A macro defined in a .c dies with its translation unit upstream but
        # would leak into every later file here. Undefining restores that.
        impl_parts += ['#undef %s' % m
                       for m in sorted(set(MACRO_DEF.findall(own[s])))]
        impl_parts.append('')

    parts = [prologue, '', '\n\n'.join(head_parts)]
    if extra_decls:
        parts += ['', extra_decls]
    parts += ['', '#ifdef %s_IMPLEMENTATION' % macro, '']
    if extra_impl:
        parts += [extra_impl, '']
    parts.append('\n'.join(impl_parts))
    parts += ['#endif /* %s_IMPLEMENTATION */' % macro, '']

    text = '\n'.join(parts)
    leftover = [ln for ln in text.split('\n')
                if re.match(r'^[ \t]*#[ \t]*include[ \t]*"', ln)]
    assert not leftover, 'unresolved local includes: %s' % leftover[:5]
    assert not has_line_comment(text), 'a // comment survived the C89 rewrite'
    assert not has_bare_inline(text), 'the C90-less `inline` keyword survived'
    return text, len(head_parts), len(order), renames


def read_text(path):
    return open(path, encoding='utf-8', errors='replace').read()


def rel(path, roots):
    for base in roots:
        try:
            r = os.path.relpath(path, base)
        except ValueError:
            continue
        if not r.startswith('..'):
            return r.replace(os.sep, '/')
    return os.path.basename(path)
