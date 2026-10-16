# minicc

[![CI](https://github.com/Om-A-osc/minicc/actions/workflows/ci.yml/badge.svg)](https://github.com/Om-A-osc/minicc/actions/workflows/ci.yml)

A complete compiler for **MiniCC**, a small object-oriented language derived from
Decaf, written from scratch in C11 with Flex and Bison. It takes a program all the
way from source text to a native executable through its own x86-64 backend, or
through LLVM IR, which lets the same program be lowered to any LLVM target,
including **NVIDIA PTX**.

```
 source ──lexer──▶ tokens ──parser──▶ AST ──sema──▶ typed AST ──irgen──▶ IR ──opt──▶ IR
                   (flex)            (bison,            (scopes, class     (3-address   (const fold,
                                      error recovery)    layout, vtables,   code over    CSE, DCE,
                                                         type checking)     basic blocks) CFG cleanup)
                                                                                           │
                                                 ┌─────────────────────────────────────────┴──────┐
                                                 ▼                                                ▼
                                        x86-64 assembly                                      LLVM IR
                                 (System V + Win64 ABIs, DWARF                         (opaque pointers)
                                  line tables, CFI)                                       │        │
                                                 │                                      clang     llc
                                              as + ld                                     │        │
                                                 ▼                                        ▼        ▼
                                           executable                                executable   PTX
```

## Features

**Front end**
- Flex lexer with line and column tracking, string escapes and literal range checks.
- Conflict-free Bison LALR(1) grammar for classes, inheritance, interfaces, arrays,
  `for`/`while`/`if`, `break`/`continue`, compound assignment, `++`/`--`, and
  implicit-`this` member access.
- Panic-mode error recovery: all syntax errors in a file are reported in one run.

**Semantic analysis**
- Scoped symbol tables (block, function, class, global) with shadowing.
- Class layout with natural field alignment, single inheritance, override signature
  checks, interface conformance checks and inheritance-cycle detection.
- Full static type checking with implicit `int` to `double` promotion inserted as
  explicit cast nodes, lvalue checks, `main` validation, and a "control reaches end of
  non-void function" analysis.

**Middle end**
- A typed three-address IR over basic blocks: single-assignment temps plus memory
  slots for variables, so no phi nodes are needed.
- Optimiser: constant folding and propagation, constant-branch elimination, local
  common subexpression elimination with store-to-load forwarding, dead-code and
  dead-store elimination, empty-block threading and block merging, run to a fixpoint.
- Short-circuit `&&`/`||` compiled to control flow; null, array-bounds and
  division-by-zero checks that report the source line at run time.

**Back ends**
- **x86-64**: GNU assembler output for both the System V (Linux) and Microsoft x64
  (Windows) calling conventions, including stack-passed arguments and mixed
  integer/double parameters. Emits `.file`/`.loc` DWARF line tables so **gdb steps
  through MiniCC source**, and `.cfi` unwind directives on ELF.
- **LLVM IR**: textual IR with opaque pointers. `clang` turns it into a native
  executable; `llc -march=nvptx64` turns it into PTX.
- Vtable-based dynamic dispatch for class types; per-class method tables and a
  runtime lookup for calls through interface types.

**Engineering**
- 45-case golden-output test suite exercising both backends at `-O0` and `-O`.
- GitHub Actions CI on Linux (gcc, clang, llc, gdb, readelf) and Windows (MSYS2).
- Pipeline inspection flags: `--dump-tokens`, `--dump-ast`, `--dump-ir`, `--opt-stats`.

## Quick start

Requirements: `flex`, `bison`, `make`, a C11 compiler (`cc`). Optional: `clang` and
`llc` for the LLVM backend and PTX, `gdb` for source-level debugging.

```sh
# Linux / macOS
sudo apt-get install flex bison gcc clang llvm gdb    # Debian/Ubuntu
make
make test

# Windows: use an MSYS2 MINGW64 shell
pacman -S flex bison make diffutils mingw-w64-x86_64-gcc mingw-w64-x86_64-clang mingw-w64-x86_64-llvm
make
make test
```

Compile and run a program:

```sh
build/minicc examples/shapes.dcf -o shapes      # x86-64 backend, assembled and linked with cc
./shapes
build/minicc -O --backend=llvm examples/shapes.dcf -o shapes   # same program via clang
```

Look inside the pipeline:

```sh
build/minicc --dump-ast   examples/arith.dcf
build/minicc --dump-ir    examples/arith.dcf       # IR straight out of irgen
build/minicc --dump-ir -O examples/arith.dcf       # after optimisation
build/minicc -S -O        examples/arith.dcf       # arith.s, x86-64 assembly
build/minicc --emit-llvm  examples/arith.dcf       # arith.ll, LLVM IR
```

Target an NVIDIA GPU through LLVM's NVPTX backend:

```sh
build/minicc --emit-llvm -O examples/shapes.dcf -o shapes.ll
llc -march=nvptx64 -mcpu=sm_50 shapes.ll -o shapes.ptx
```

Debug at the source level (the x86 backend emits DWARF line tables with `-g`):

```
$ build/minicc -g examples/arith.dcf -o arith
$ gdb -q arith
(gdb) break mcc_fib
Breakpoint 1 at 0x1189: file examples/arith.dcf, line 3.
(gdb) run
Breakpoint 1, mcc_fib () at examples/arith.dcf:3
3       int fib(int n) {
(gdb) next
4           if (n < 2) return n;
(gdb) bt
#0  mcc_fib () at examples/arith.dcf:4
#1  0x... in mcc_main () at examples/arith.dcf:20
```

User functions are emitted as `mcc_<name>`, methods as `mcc_<Class>__<method>`.

## What the optimiser does

`tests/opt/fold.dcf`:

```c
int compute(int a, int b) {
    int unused = a * 100;          // dead store
    int c = 2 + 3 * 4;             // folded to 14
    int d = (a + b) * (a + b);     // a + b computed once
    int e = (a + b) - c;           // reuses a + b and the forwarded constant
    if (10 > 20) { d = -1; }       // branch folded away
    if (true) e = e + 1;           // condition folded, blocks merged
    g = d + e;
    return g;
}
```

`build/minicc --dump-ir -O tests/opt/fold.dcf` produces a single straight-line block:

```
  L0:
    %t0 = load i32 %s0
    %t5 = load i32 %s1
    %t6 = add i32 %t0, %t5
    %t10 = mul i32 %t6, %t6
    %t15 = sub i32 %t6, 14
    %t19 = add i32 %t15, 1
    %t22 = add i32 %t10, %t19
    store @mccg_g <- i32 %t22
    ret i32 %t22
```

## The language

MiniCC is Decaf with a few C conveniences added.

| Area | Constructs |
| --- | --- |
| Types | `int`, `double`, `bool`, `string`, class and interface types, `T[]` arrays, `null` |
| Declarations | globals, locals with initialisers (anywhere in a block), functions, `class C extends P implements I, J { fields; methods }`, `interface I { prototypes }` |
| Statements | `if`/`else`, `while`, `for` (with declaration in the init clause), `break`, `continue`, `return`, `Print(...)`, blocks |
| Expressions | arithmetic, comparison, `&&` `||` `!`, assignment and `+= -= *= /= %=`, `++`/`--` (prefix and postfix), `obj.field`, `obj.method()`, `arr[i]`, `arr.length()`, `New(C)`, `NewArray(n, T)`, `ReadInteger()`, `ReadLine()`, `this`, string `+` and `==` |
| Semantics | `int` promotes to `double` implicitly; `null` is assignable to class, interface and array types; a subclass is assignable to its superclasses and implemented interfaces; method overrides must match signatures exactly |

Runtime errors (null dereference, out-of-bounds index, division by zero) terminate
the program with a message naming the source line and exit code 1. See
[`examples/`](examples/) and [`tests/run/`](tests/run/) for programs.

## Project layout

```
src/lexer.l        Flex scanner
src/parser.y       Bison grammar, builds the AST, error recovery
src/ast.[ch]       AST node types, constructors, --dump-ast printer
src/sema.[ch]      symbol tables, class layout, vtable slots, type checking
src/ir.[ch]        IR data structures, printer, runtime function table
src/irgen.[ch]     AST -> IR lowering (control flow, checks, dispatch)
src/opt.[ch]       optimisation passes
src/emit_x86.[ch]  x86-64 code generator (System V / Win64, DWARF lines, CFI)
src/emit_llvm.[ch] LLVM IR emitter
src/main.c         driver: options, pipeline, invokes cc / clang
src/util.[ch]      arena allocation, vectors, string map, diagnostics
runtime/runtime.c  allocation, arrays, strings, I/O, interface lookup, C main
tests/             golden-output tests (parse, sema, opt, run) and the runner
examples/          sample programs
.github/workflows  CI for Linux and Windows
```

## Design notes

**IR shape.** Temps are assigned once; variables live in slots accessed only by
`load`/`store`. This keeps the IR phi-free while still letting the optimiser treat
temps as values. The LLVM backend maps slots to `alloca` (LLVM's `mem2reg` promotes
them), the x86 backend maps slots and temps to stack frame locations.

**Object model.** An object is a vtable pointer followed by fields at naturally
aligned offsets; a subclass extends its parent's layout and vtable, with overriding
methods replacing the inherited slot. Calls on class-typed receivers index the
vtable directly. Interfaces are dispatched through a per-class table of
`(method id, function)` pairs searched by `__mcc_lookup`, which avoids itables while
keeping class-typed calls fast.

**Code generator.** The x86 backend deliberately performs no register allocation:
every value is spilled, each IR instruction loads its operands into scratch
registers, computes, and stores. A one-register peephole elides the reload of a
value that is already in `rax`/`xmm0`. Correctness and ABI handling were the goal;
a linear-scan allocator is the obvious next step.

**Checks.** Every pointer dereference is preceded by a null check and every element
access by an unsigned bounds compare (which also rejects negative indices). The
checks are ordinary IR, so the optimiser folds them away when the index or pointer
is a known constant.

## Testing

```sh
make test                  # or: bash tests/run_tests.sh
BACKENDS=x86 make test     # skip the LLVM backend when clang is not installed
UPDATE=1 bash tests/run_tests.sh   # regenerate golden files after an intentional change
```

Parse and sema tests compare diagnostics, opt tests compare optimised IR, and run
tests compile each program with every backend at `-O0` and `-O`, run it against an
optional `.in` file, and compare stdout, stderr and the exit code.

## Limitations and future work

- No register allocation in the x86 backend; a linear-scan allocator over the IR temps
  would remove most memory traffic.
- No garbage collection: objects, arrays and strings are heap-allocated and never freed.
- DWARF covers line tables only; variable and type information (`.debug_info`) is not
  emitted, so `print x` in gdb does not work yet.
- `double` has no modulo, there is no cast syntax and no `switch`.
- Optimisations are local to a basic block. Global value numbering, loop-invariant
  code motion and devirtualisation of calls to final methods are natural extensions.

## History

The project began as a course assignment: a Flex lexer and Bison parser that accepted
or rejected a Decaf-like language (kept as `examples/legacy.dcf`). Everything after the
parser, and the parser's AST construction, error recovery and conflict-free grammar,
were added afterwards.
