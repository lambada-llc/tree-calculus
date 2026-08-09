The iterative variant. See [../README.md](../README.md) for what all the WASM
implementations share: the I/O model, usage, build and test.

## Why it exists

The recursive variants ([../eager-value](../eager-value/),
[../eager-nil](../eager-nil/)) reduce, parse and print by recursing on the
machine stack, so their depth is whatever the engine grants — under Node that
is roughly 10⁴–10⁵ frames before a `RuntimeError`, which is not enough for
most of [the benchmark suite](../../../benchmark/) (silly-exp and
exercise-rules die there; historically the failures surfaced as outright
segfaults, but that part was the `node:wasi` runner — see the shared README).

This variant runs the same reduction with no recursion at all, so depth is
bounded only by memory. It is the WAT port of
[../../cpp/eager-ternary-nil-vm-32.hpp](../../cpp/eager-ternary-nil-vm-32.hpp):
`apply` becomes a loop over explicit continuation frames in linear memory, and
parsing and printing become loops over the same frame region. It is the one
WASM variant that completes the whole benchmark suite.

## Node representation

Nodes are [../eager-nil](../eager-nil/)'s tagless 8-byte pair, arity
discriminated by null children. The arena sits above a reserved ~64 MB frame
region, so a node is named by its absolute byte address rather than
eager-nil's `0x10`-biased offset; the encoding of trees in memory is otherwise
identical.

## The frame machine

The three recursive call sites of `apply` become two frame types, tagged in
bit 0 of the first word (nodes are 8-aligned, so the bit is free):

```
APPLY_TO(arg)               when the current computation produces r,
                            compute apply(r, arg)
COMPUTE_AND_APPLY(fn, arg)  when the current computation produces r,
                            push APPLY_TO(r) and compute apply(fn, arg)
```

The second is the shape `apply(apply(fn, arg), r)` of the fork-stem rule.
Frames are 8 bytes; the ~64 MB region holds 8M of them, and exhausting it
traps cleanly (`unreachable`) rather than crashing the host. Parse frames are
child slots still awaiting a subtree; emit frames are nodes still to visit
(ternary being exactly a left-first preorder walk).

Byte-at-a-time WASI reads and writes also become 64 KB buffers — with no
recursion to blame for I/O cost, there was no reason to keep paying one host
call per byte.

## Measurements

Within a few percent of the recursive eager-nil where both run (recursive
fib n=26 under Node 22, best of 5, Linux container: 1.81s against 1.71s) —
the price of pushing a frame where the recursive variant pushes a machine
frame for free. What it buys: silly-exp (n=16) and exercise-rules (n=200000)
complete instead of overflowing, as does any workload the C++ VM evaluators
can run.
