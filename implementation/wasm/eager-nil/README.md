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

The time it buys is engine-dependent. On recursive fib, n=24..28:

| engine   | vs `eager-value`                     |
| -------- | ------------------------------------ |
| wasmtime | ~2.8x faster                         |
| Node/V8  | within ~10% (marginally *slower*)    |

So measure on whichever engine you care about. Two things that did not explain
the gap, in case they look tempting: alignment (padding the tagged node to an
aligned 16 bytes is no faster under wasmtime than the unaligned 12) and
allocation count (identical in both).
