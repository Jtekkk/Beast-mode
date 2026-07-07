# Beast Design Notes

Why Beast is shaped the way it is, and how it earns the word "efficient."
This is the companion to `SPEC.md` (what the language *is*) — this document is
about *why*, and about the reference VM's architecture.

## Philosophy

Speed in Beast comes from **value representation and dispatch**, not from an
optimizing compiler. The compiler is single-pass and allocates no AST; almost
all of the performance is structural, decided once in the layout of a `Value`
and the shape of the interpreter loop. That is what lets a small, readable C
program consistently beat CPython on compute-bound work.

Three commitments follow from that:

1. **Numbers are never boxed.** Arithmetic and loop counters allocate zero
   heap memory, ever.
2. **The hot path has no hash lookups.** Locals and globals resolve to array
   indices at compile time.
3. **The core is small and orthogonal.** Functions, closures, arrays, maps,
   and strings, done well, beat a large feature list done adequately.

## The efficiency mechanisms

### NaN-boxing (`value.h`)

A `Value` is a single `uint64_t`. IEEE-754 doubles are stored verbatim; every
non-number is packed into the quiet-NaN bit space. Singletons (`nil`, `true`,
`false`, and three table-internal sentinels) are distinct NaN patterns; heap
objects are the sign bit + QNAN + a 48-bit pointer. This buys:

- 8-byte values — twice the density of a tagged union, so more of the working
  set stays in cache.
- Zero-allocation numbers — the biggest structural win over CPython, which
  boxes every integer.
- A one-instruction type test (`(v & QNAN) != QNAN` is "is a number").

A tagged-union fallback (`-DBEAST_NO_NAN_BOXING`) keeps the VM correct on
platforms where the 48-bit-pointer assumption does not hold, and it is built
and tested in CI so it never rots.

### f64-only arithmetic

There is one numeric type. `OP_ADD`'s fast path is two type checks, one
`addsd`, and one store — no int/float promotion lattice, no overflow-to-bignum
branch, no boxed integers. The cost is exactness above 2^53 and no bit
manipulation; both are documented and, for a scripting language, rarely
binding.

### Computed-goto dispatch (`vm.c`)

Under GCC and Clang the interpreter uses direct-threaded dispatch
(labels-as-values): each opcode ends by jumping straight to the next, so the
CPU's branch predictor gets a separate prediction site per opcode instead of
one shared, near-random switch. A portable `switch` fallback compiles
everywhere. `ip` is held in a local (register) across the loop.

### Fused opcodes, a fixed set

Beast does not have an open-ended superinstruction generator — it has two
carefully chosen fusions that carry the common cases:

- **`OP_FOR_RANGE`** performs increment, limit test, and backward branch in a
  single dispatch. A `for i in a..b` loop pays one opcode of overhead per
  iteration instead of the six or seven a naive lowering would. This single
  opcode is most of the reason the `loop` benchmark runs ~8× faster than
  CPython.
- **Compare-and-branch fusion.** The compiler's peephole rewrites a comparison
  immediately followed by a conditional jump (`if a < b`, `while i < n`) into
  one `OP_JUMP_IF_NOT_LESS`-style opcode, halving the work at every loop
  condition.

### Compile-time slot addressing (`compiler.c`)

Locals are stack-slot indices. Globals are indices into a flat array, resolved
when the name is first seen and bound late so any declaration order works.
Neither a local nor a global read touches a hash table at run time — a probe
CPython pays on every global access.

### Zero-copy calls

A call is a pointer bump plus one arity compare. The callee's slot window
overlaps the caller's argument slots on one contiguous value stack; frames are
`(closure, return ip, slot base)` in a fixed array. No allocation happens per
call.

### Interned strings (`object.c`, `table.c`)

Every string is interned in a VM-owned set at creation, with its FNV-1a hash
cached in the header (one allocation per string via a flexible array member).
So string equality and map-key comparison are pointer comparisons, and map
keys never re-hash. This is also why `map.field` sugar is free: it is a
constant-string key looked up by cached hash.

The tradeoff is real and shows up in the benchmarks (see below): concatenating
strings in a tight loop hammers the intern table. The designed-in mitigation
(intern lazily, only when a string is actually used as a map key) is a v0.2
item; for v0.1 the honest guidance is to collect and join.

### Mark-sweep GC (`memory.c`)

A precise, non-moving mark-sweep collector. There is no reference-count
traffic on any instruction — the whole point of unboxed numbers is that a
numeric loop triggers *zero* collections. Roots are the value stack, call
frames, open upvalues, the globals array, compiler temporaries, and a small
temp-root stack that natives use to protect a freshly allocated object across
a second allocation. `next GC` is set to twice the surviving live bytes.

GC correctness is the dominant bug surface in a VM like this, so it is guarded
from day one: `BEAST_GC_STRESS=1` collects on *every* allocation, and the test
suite is run in that mode plus under ASan/UBSan in CI.

## The compiler

A single-pass Pratt parser emits bytecode directly — no AST. It performs
exactly two optimizations, both essentially free:

1. **Constant folding** of `literal op literal` at emit time.
2. The **compare-and-branch peephole** described above.

That is the entire optimizer, by design. The compiler's ceiling (no inlining,
no dataflow) is accepted deliberately: the speed lives in the value
representation and the dispatch loop, not here.

Diagnostics are treated as a feature. Compile errors carry `file:line:column`,
the source line, a caret, and a fix hint; an undefined global produces a
Levenshtein-based "did you mean". Runtime errors carry a full stack trace.

## What was deliberately cut

The panel that designed Beast cut, with reasons, everything that would have
threatened the size budget or the benchmark story: **classes/OOP** (maps with
dot-sugar plus closures cover records and objects), **exceptions** (they
thread cost through every frame; use `nil`-return + `assert`), **modules**,
an **integer type** and **bitwise operators**, **string interpolation**,
**general `for-in`/iterators**, **varargs/defaults/keyword args** (they
destroy the one-compare arity check), **tail calls and a JIT**, and
**operator overloading** (it would put a dynamic lookup inside `OP_ADD`).

Several of these are reserved words (`class const import match try`) so a
later version can add them without breaking existing code. Nothing is
parsed-and-ignored; using a reserved word is a clear error today.

## Benchmarks — the honest table

Generated by `make bench` (best-of-5), Python 3.11, one representative
machine. Reproduce with `bench/run_bench.sh`. Speedup is Python time ÷ Beast
time; above 1.0 means Beast is faster.

| Benchmark   | What it stresses            | Speedup vs CPython |
|-------------|-----------------------------|--------------------|
| `loop`      | Tight numeric range-for     | ~8.7×              |
| `fib`       | Function-call throughput    | ~1.9×              |
| `wordcount` | Map insert/lookup churn     | ~1.9×              |
| `trees`     | Allocation / GC churn       | ~1.6×              |
| `sieve`     | Array index get/set         | ~1.1×              |
| `strings`   | String concat in a loop     | ~0.3× (slower)     |

The wins are where the architecture pays off: the fused range-for opcode and
unboxed arithmetic dominate `loop`; zero-copy calls carry `fib`; cached-hash
interned keys carry `wordcount`. The loss is equally honest: eager string
interning makes concat-in-a-loop pay a hash and a table probe every time,
which is exactly the tradeoff noted above. CPython 3.11+'s specializing
interpreter has closed many microbenchmark gaps, so `sieve` is close to a tie.

Publishing the loss is the point. Beast aims to be the fastest *honest*
small interpreter, not to win a curated table.
