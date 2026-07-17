# Breaking the tree-calculus reduction records

Notes on making triage/tree-calculus reduction faster, with **measured results**
where a prototype exists and **honest negative findings** where an idea sounds
good but doesn't pay off on the current benchmark suite.

## Status (2026-07-16): two of these ideas are now in-tree

Two approaches explored here — a cache-dense node representation and a
multi-step "peeking" reducer — were independently implemented in the main C++
tree and are now the fastest evaluators in the repo, beating the hand-golfed
x86 assembly:

- **Representation** → `eager-ternary-nil-mmap-32.hpp`: tagless 8-byte nodes
  (two 32-bit children, arity by null-child) in an `mmap` arena. Same win this
  note originally prototyped as a single-word packed node — half the memory
  traffic per node, base pointer pinned in a register.
- **Peeking** → `peek.hpp` (`Peek<Base>` mixin) + `reduce-recursive.hpp`: the
  exact rule-2 expansion below, layered on any backend via `triage`. Combined as
  `eager-ternary-nil-mmap-32-peek`, it leads every compute-heavy benchmark
  (upstream log `2026-07-16-03-30`: fib24 0.141 vs ASM 0.157; merge-sort 0.237
  vs 0.283).

So the standalone `eager-packed` / `eager-peek` prototypes that used to live here
were **superseded and removed** — the in-tree mixin versions are cleaner
(composable over any representation) and at least as fast. The peeking
derivation is preserved below because it documents *why* the win exists; the
sections after it cover avenues that are **not** yet in-tree.

### The peeking derivation (now realized in `peek.hpp`)
Rule 2 duplicates `b` into both branches:
`apply(fork(stem x, y), b) = apply(apply(x,b), apply(y,b))`. Symbolically
normalizing the RHS on the tag of `x` (with `R := apply(y,b)` built lazily):

```
x = leaf              -> fork(b, R)
x = stem(leaf)        -> b                 <== R = apply(y,b) is provably DEAD, never built
x = stem(stem x2)     -> apply(apply(x2,R), apply(b,R))
x = stem(fork(w,x2))  -> triage R: leaf->w | stem d->apply(x2,d) | fork d e->apply(apply(b,d),e)
x = fork(leaf, x2)    -> apply(x2, R)
x = fork(_, _)        -> apply(apply(x,b), apply(y,b))   // generic fallback
```

Two wins: collapsed cases skip intermediate node allocation, and the K-like head
(`x = stem(leaf)`) makes the duplicated branch dead so it is never reduced — "the
S rule combined with a K that eliminates one copy." Measured earlier: ~28–30%
fewer applies, the drop firing 0.5–1.25M times per run. The natural next step
(not yet done) is to **generate** these rules by a symbolic reducer for peek
depth 3, 4, … instead of hand-deriving depth 2.

---

The remaining sections are ideas **not** in the main tree — a prototype plus
designs, with honest notes on where each does and doesn't pay off.

## Memoization — concrete fails, schematic is what won ⚠️

The first instinct — cache `apply(a,b) → c` for concrete tree ids, with
hash-consing so equal trees share ids — **loses on this suite**. Instrumentation
shows why: these programs have almost no reusable subproblems.

| program   | distinct sub-applies | growth |
|-----------|----------------------|--------|
| fib(n)    | ~C·φⁿ (φ≈1.618)      | exponential in n |
| silly-exp | ~2ⁿ distinct nodes   | exponential in n |

So the memo table just grows without producing hits; every lookup is pure
overhead (the prototype ran fib24 in 1.18s vs 0.35s for the plain evaluator).
The OCaml prototype in `implementation/ocaml/lib/memoize.ml` shows concrete memo
is an *unbounded* win on programs with real overlap (naive fib(64): 1.3e16
theoretical applies → 65k real), but the benchmark programs are, by nature,
adversarial to it.

The lesson that generalizes: **schematic** memoization — caching rule schemas
over metavariable holes (the `peek.hpp` approach above) — is the version that
wins, because abstract patterns match constantly. Concrete memoization should
only ship as an opt-in for workloads known to repeat (interactive REPLs,
DP-shaped programs). (A standalone `eager-value-memo` prototype demonstrated the
concrete version; it was dropped when the C++ evaluators moved to
template-callable `triage`, which its `std::function` interface predated, and
because it loses here anyway. The finding stands on the OCaml evidence.)

## Parallel reduction — beats the single-thread record ✅

**Files:** `implementation/cpp/parallel-peek.cpp` (champion backend + fork-join),
`implementation/cpp/parallel-packed.cpp` (earlier plain-backend prototype). Both
OpenMP, self-contained.

Tree calculus is confluent, so the two independent sub-applies of rule 2
(`apply(x,b)` and `apply(y,b)`) can run on different cores — a natural fork-join.
`parallel-peek` puts this on the *champion* backend: nil-mmap-32 nodes + the
peeking super-rules (incl. K-elimination) + fork-join at the two independent
sub-applies, with a shared `mmap` arena and lock-free per-task bump chunks.

Scaling depends entirely on the *workload*:

- **fib / silly-exp / merge-sort: flat** (t1≈t4). These are fold/fixpoint-bound:
  the interpreter's duplication rule is fine-grained and *unbalanced* (fib's two
  branches differ in size by φ), and the fixpoint serializes the traversal, so one
  task gets nearly all the work. silly-exp does scale ~2.7× once n is large enough
  (n=20), but its 2ⁿ-leaf *output* dominates and dilutes the win.
- **A purpose-built independent-work benchmark scales and beats the record.** See
  `benchmark/parallel.sh` / `benchmark/parallel-and.ternary` (generated by
  `implementation/typescript/src/gen-parbench.mts`). The program is
  `\n. balancedAnd([ equal (exp n) (exp n) ] × WIDTH)`: WIDTH independent expensive
  predicates, a *balanced* AND combine (log-depth, cheap), and a **single-bit
  output** so nothing large is printed. Measured `parallel-peek` (WIDTH=16, n=14,
  4-core x86_64; single-thread champion `nil-mmap-32-peek` = 0.40s):

  | threads | 1 | 2 | 3 | 4 |
  |---------|---|---|---|---|
  | time    | 0.42s | 0.25s | 0.20s | **0.16s** |
  | self-speedup | 1.00× | 1.70× | 2.13× | **2.69×** |
  | vs champion  | 0.96× | 1.63× | 2.04× | **2.58×** |

  So on 4 cores it runs the workload **2.6× faster than the fastest single-thread
  evaluator** — parallelism finally beats the record, not just the plain backend.
  Single-thread overhead is negligible (t1 ≈ champion) because the no-spawn hot
  path is inlined and pays no atomic. The earlier `parallel-packed` (plain backend)
  only reaches ~0.73× of the champion even at t=4 — which is exactly why porting the
  fork-join onto the champion representation was the necessary step.

  This is the shape a parallel reducer wants — the prime-check idea (many
  independent tests → one bit) without needing modular arithmetic. Design notes:
  the eager evaluator reduces *both* arguments of `and` to values before combining,
  so the short-circuit in `and`/`any` does **not** serialize the leaves (a common
  misconception); and the combine must be a *balanced* tree, not a sequential
  right-fold, to keep recombination off the critical path.

Further headroom: granularity is a fixed branch-depth cutoff (BCUT) + a task cap;
a real work-stealing deque with cost-estimated splitting would push past 2.6×
toward linear, and would also let the fold/fixpoint programs expose whatever
parallelism they have.

```sh
bash benchmark/parallel.sh              # scaling table vs champion (builds as needed)
N=16 BCUT=5 bash benchmark/parallel.sh  # heavier per-leaf work
```

## GPU reducer — design 🧭

A GPU wins when there are many independent reductions in flight — the same
data-parallel regime as the parallel section, at ~1000× the width.

- **Representation:** the term graph as structure-of-arrays in global memory
  (the 8-byte node packs perfectly), plus an explicit worklist of ready redexes.
- **Kernel loop (bulk-synchronous):** each thread classifies one redex by the 5
  rules (a 3-way tag `switch` — little divergence); allocate results with a
  per-warp `atomicAdd` bump; append newly-ready redexes to the next frontier via
  warp-ballot + prefix-sum compaction; repeat until empty.
- **Hash-cons / memo on GPU:** a lock-free open-addressing table (`atomicCAS`)
  gives structural sharing *and* frontier dedup, so the same redex isn't fired by
  two warps — this is where the schematic-sharing idea and the GPU meet.
- **Hard parts:** irregular frontier width (≈1 for fold/fixpoint programs, huge
  for data-parallel ones), allocation contention, and footprint (silly-exp
  materializes 2ⁿ nodes — must stay a shared DAG). Pragmatic first cut: offload
  only a wide independent map/fold as a batched kernel, keep the rest on the host.

## Compile, don't interpret — idea

The benchmark left-folds a *fixed* program over its input. Partial-evaluate /
stage that program into a specialized reducer (bytecode or JIT'd machine code) so
the inner loop drops tag dispatch entirely — the supercombinator / STG route.
This is the most promising way to beat even `eager-ternary-nil-mmap-32-peek`
single-threaded, and it composes with the peeking super-rules (compile them in).

---

## Summary

| approach | status |
|----------|--------|
| Cache-dense representation | **in-tree** as `eager-ternary-nil-mmap-32` (beats ASM) |
| Schematic peeking super-rules | **in-tree** as `peek.hpp`; next: generate depth 3+ |
| Concrete memoization | loses here (no overlap); opt-in only — schematic is the win |
| Parallel fork-join | `parallel-peek.cpp` (champion backend + fork-join); flat on fib/fold, **2.6× on 4 cores — beats the single-thread record** on the purpose-built `parallel.sh` workload |
| GPU frontier reducer | design |
| Compile-don't-interpret | idea; most promising further single-thread win |

Transferable lesson: the two levers that beat the assembly were making the
unavoidable work cheaper (representation) and doing less of it (schematic
super-rules that skip provably-dead duplicated work). Concrete memoization and
parallelism only pay off on programs with the right structure and belong as
opt-in layers.
