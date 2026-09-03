# C code style

This document describes style for C code in this repository. It supplements local
comments and headers; when a file has a stricter portability requirement, the
stricter requirement wins.

## Language boundary

The runtime and module code targets **C89 + POSIX**. Keep that boundary real:

- declare variables at beginning of each block;
- use `/* ... */` comments, not `//` comments;
- do not use designated initializers, compound literals, declarations in `for`,
  `stdbool.h`, variadic macros, or other C99 syntax in runtime code;
- use POSIX APIs only where the file already declares its POSIX feature level;
- keep Windows code behind the existing platform branches;
- use `sprintf` only where the existing portability contract permits it, and use
  the local bounded string helpers for paths and externally supplied text.

`nob.c` and `nob.h` are build-time tooling and explicitly use C99. Do not copy
that exception into `lib/`, `modules/`, or the portable front ends.

Every public header states its portability target near its header guard. New
headers should do the same.

## Names and layout

- Use `osr_` for POSIX core symbols, `osrm_` for module entry points, and the
  established subsystem prefix for subsystem-private symbols.
- Use `PascalCase` for public typedef names (`BenchOpts`, `PwrMeter`) and
  `snake_case` for functions and variables.
- Put the implementation comment and portability note at the top of each new
  translation unit.
- Put the interface in a header when another translation unit needs it. Keep
  helpers `static` when they are private to one translation unit.
- Keep declarations in headers short and explain ownership, return values,
  failure behavior, and lifetime there. Existing headers such as
  `lib/module.h` and `lib/bench/bench.h` are the model.
- Prefer tables of data plus one loop over repeated platform or module branches.
  Existing registries in `osr.c`, `lib/modules.c`, and `lib/build.c` demonstrate
  this pattern.

## Structures and initialization

Use structures for state that belongs together, not as a general replacement
for every short function call. A structure is a good fit when:

1. several parameters describe one operation;
2. fields have clear names and ownership;
3. more fields are likely to arrive;
4. the callee needs one coherent context across helper calls.

C89 has no named arguments. This is **not** valid C89 (and `struct {};` is not a
portable C type):

```c
func((struct Options){ .var1 = "var", .var2 = a });
```

Use a named type, an initializer helper, and a pointer instead:

```c
typedef struct {
    const char *var1;
    int var2;
    int has_var2;
} FuncOptions;

void func_opts_init(FuncOptions *opts);
int func(const FuncOptions *opts);

FuncOptions opts;
func_opts_init(&opts);
opts.var1 = "var";
opts.var2 = a;
opts.has_var2 = 1;
func(&opts);
```

The initializer helper makes defaults explicit and avoids treating an all-bits-
zero object as a portable null pointer representation. Keep `has_*` flags when
zero, empty string, `NULL`, or false is also a meaningful caller value. If the
API guarantees that `NULL` means “use defaults”, document it and accept a null
options pointer; otherwise require a non-null pointer and validate it.

Prefer `const Options *` when the callee only reads options. Do not retain that
pointer unless the API explicitly transfers ownership or requires caller-owned
storage to outlive the operation. A pointer to a local options object is valid
only for the duration of the call. For a small, stable, value-like object,
pass-by-value can be clearer; do not choose it only to imitate named arguments.

Do not expose a struct by value as an ABI-stability promise. Adding a field
changes its size and calling convention. For public or shared-library surfaces,
prefer a pointer plus an initializer and document struct versioning if needed.

Do not create an options struct for two obvious, stable scalar parameters. Do
not create a one-use wrapper merely to reduce one line at one call site. First
look for an existing subsystem type and extend it when it already owns the
invariant.

## Where this pattern fits here

The repository already has the right example: `BenchOpts` in
`lib/bench/bench.h:160` and `bench_cpu(const BenchOpts *opts, BenchResult *r)`.
`lib/benchmark.c:248` initializes that object, assigns CLI overrides, and passes it
read-only to the benchmark engine. Keep this shape. If benchmark configuration
grows, add fields and update `bench_opts_init`, rather than extending `bench_cpu`
with another positional argument.

Good future candidates, only when their interfaces grow or cross another layer:

- a benchmark run configuration beyond `seconds`, `verbose`, and `announce`;
- a module execution context if the repeated `repo_root`, `map_path`, `theme`,
  and user data currently passed through install orchestration become shared by
  several helpers (`lib/install.c`'s runner);
- a builder request if source builders acquire multiple optional knobs. Keep
  the builder registry table in `lib/build.c` data-driven; do not wrap each
  one-argument builder today;
- a render/apply request if template, theme, destination, and ownership policy
  start traveling together across `lib/config.h` and `lib/render.h`.

Existing state types such as `PwrMeter`, `BenchResult`, `UvCaps`, manifest
records, and theme records already group durable state. They are not optional
argument objects and should not gain fields merely to make calls look uniform.
Likewise, simple file, package, service, and command helpers in
`lib/module.h` are clearer as direct calls while their parameter sets remain
small and stable.

## Macro policy

Macros are compile-time tools, not a substitute for functions or an options
syntax. Use them when they make a stable invariant visible at the call site:

- include guards;
- compile-time constants and platform selection;
- array-count expressions for actual arrays (`sizeof(array) / sizeof(array[0])`);
- small, side-effect-free, parenthesized expressions;
- statement wrappers that must behave as one statement, using `do { ... } while (0)`;
- test assertions and repetitive test plumbing, where the macro must capture
  the caller's file and line.

Existing good uses include `COMMAND_COUNT` in `osr.c`, `MODULE_COUNT` in
`lib/modules.c`, subsystem limits such as `BENCH_PATH_MAX` and `OSR_PATH_MAX`,
and the assertion macros in `test/c_test.h`. Keep count macros next to the array they describe.
Use an enum or a `static const` object when a typed value or debugger visibility
is more useful than preprocessing.

Avoid macros that:

- evaluate an argument more than once;
- hide control flow, allocation, I/O, locking, or ownership;
- encode a pseudo-function with an undocumented type or lifetime;
- paste a long argument list into a call;
- create an options object with `OPTS(...)` or similar.

C89 has no standard variadic macros, and a macro would hide the options type,
default initialization, validation, and compound-object lifetime. Write the
few explicit assignment lines instead. The reader should see which type is
passed and which defaults are active.

Avoid token-list convenience macros such as a package list unless the list is
truly a named, immutable concept used in several places. Prefer a local
`static const char *const names[]` terminated by `NULL`, matching module code
such as `modules/flameshot.c`.

## Calls, errors, and ownership

- Keep calls readable. Wrap long argument lists at semantic boundaries and put
  one argument per line when that improves reviewability.
- Return the established subsystem convention: module and operation APIs use
  `1` for success and `0` for failure unless their header documents another
  result. Document special negative statuses.
- Validate paths, counts, indexes, and nullable inputs at the boundary that
  owns the invariant. Do not duplicate checks in every caller.
- Make ownership explicit in names and comments. Borrowed strings and arrays
  must remain valid for the documented call; copied data must say so.
- Preserve idempotency and the single fatal/non-fatal behavior documented by the
  subsystem. Do not hide a mutation inside a convenience macro.

## Review checklist

Before adding a struct or macro, ask:

1. Does an existing type or helper already own this data?
2. Does this reduce positional ambiguity without hiding behavior?
3. Can a C89 reader understand initialization, defaults, validation, and
   lifetime by reading the call site?
4. Does the public signature remain easy to extend without an ABI trap?
5. Are tests covering omitted values, explicit zero/false values, invalid input,
   and repeated execution where those cases apply?

If the answer is not clearly yes, keep the direct function call and ordinary C
statements.
