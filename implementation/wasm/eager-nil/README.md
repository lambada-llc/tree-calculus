The tagless-node variant. See [../README.md](../README.md) for what all the WASM
implementations share: the I/O model, usage, build and test.

## Node representation

The same evaluator as [../eager-value](../eager-value/), differing only in how a
node is stored. `eager-value` tags each node with its arity (`type`, `u`, `v` —
12 bytes); this variant drops the tag and discriminates on null (0) children
instead, the "nil" representation of
[../../cpp/eager-ternary-nil-32.hpp](../../cpp/eager-ternary-nil-32.hpp):

```
<0> <0>      — leaf
<child> <0>  — stem
<a> <b>      — fork  (both non-null)
```

Nodes are therefore a constant 8 bytes, offset 0 is reserved as the null
sentinel, and the one shared leaf sits at offset 8.

## Measurements

The memory saving is exact and engine-independent — a third less, since node
counts are identical and only the stride changes. Recursive fib, n=28: 832 MB of
arena against `eager-value`'s 1216 MB.

Speed is not what it buys. Compared like for like — same dispatch shape, only
the node layout differing — the two are within a few percent, this one typically
a hair slower, because a triage here reads two child slots and tests both where
a tagged node answers from one field:

| workload (Node/V8, best of 5, incl. ~0.04s process startup) | eager-value | eager-nil |
| --- | --- | --- |
| recursive fib, n=26 | 0.40s | 0.43s |
| merge-sort, n=2000 | 0.32s | 0.32s |

One caveat if you time this against `eager-value` as it stands: under wasmtime
that variant is ~2.7x slower than this one, and none of that gap is the node
size. It is the `br_table` its triage dispatches with, which Cranelift compiles
badly; a tagged evaluator that dispatches with an `if` chain instead lands on
this variant's time (fib n=26: 0.75s vs 0.81s). Node/V8 shows no such penalty,
which is why the two engines appeared to disagree.

Two other things that did not explain anything, in case they look tempting:
alignment (padding the tagged node to an aligned 16 bytes is no faster under
wasmtime than the unaligned 12) and arena growth (preallocating 1 GB so
`memory.grow` never runs changes no result above).
