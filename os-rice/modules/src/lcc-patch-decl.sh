#!/bin/sh
# modules/src/lcc-patch-decl.sh -- one-time, idempotent patch of lcc's rcc
# front end (src/decl.c) so it tolerates an *identical* typedef
# redefinition, the way gcc does. Modern glibc depends on that tolerance:
# with _XOPEN_SOURCE set, glob.h typedefs size_t and stddef.h typedefs it
# again a few headers later. C11 permits the identical redeclaration; the
# 2002 C89 rcc hard-errors. This is the one C11 nicety rcc must absorb to
# live on a 2026 glibc host. Usage: lcc-patch-decl.sh SRC/decl.c
set -e
f="$1"
[ -f "$f" ] || { echo "lcc-patch-decl.sh: no such file: $f" >&2; exit 1; }

# 1) Insert the typedef_compatible() helper right after the rcsid line.
if ! grep -q 'typedef_compatible' "$f"; then
    perl -0pi -e 's%static char rcsid\[\] = "\$Id\$";%static char rcsid[] = "\$Id\$";\n\n/* typedef_compatible -- do ty1 and ty2 denote the same type?  eqtype() treats\n * scalars as incomparable (case INT/UNSIGNED/FLOAT: return 0), so scalars are\n * compared by op + size here; the composite cases mirror eqtype().  Used to\n * accept an identical typedef redeclaration (C11), which modern glibc relies\n * on: glob.h and stddef.h both typedef size_t under _XOPEN_SOURCE. */\nstatic int typedef_compatible(Type ty1, Type ty2) {\n    if (ty1 == ty2) return 1;\n    if (ty1->op != ty2->op) return 0;\n    switch (ty1->op) {\n    case INT: case UNSIGNED: case FLOAT:\n        return ty1->size == ty2->size;\n    case POINTER:\n        return typedef_compatible(ty1->type, ty2->type);\n    case CONST: case VOLATILE: case CONST+VOLATILE:\n        return typedef_compatible(ty1->type, ty2->type);\n    case ENUM: case UNION: case STRUCT:\n        return ty1->u.sym == ty2->u.sym;\n    case ARRAY:\n        return ty1->size == ty2->size && typedef_compatible(ty1->type, ty2->type);\n    default:\n        return eqtype(ty1, ty2, 1);\n    }\n}%' "$f"
fi

# 2) Relax the same-scope typedef redeclaration check in dcl(): identical
#    redefinition is fine, anything else still errors as before.  (Note: in
#    the replacement, quantifiers do not apply, so the five tabs of indentation
#    are written out as \t\t\t\t\t rather than \t{5}.)
perl -0pi -e 's~\t{5}if \(p && p->scope == level\)~\t\t\t\t\tif (p && p->scope == level\n\t\t\t\t\t    && !(p->sclass == TYPEDEF && typedef_compatible(p->type, ty1)))~' "$f"
