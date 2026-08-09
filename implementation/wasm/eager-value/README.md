The tagged-node variant. See [../README.md](../README.md) for what all the WASM
implementations share: the I/O model, usage, build and test.

## Node representation

Each node carries its arity explicitly, as a `type` field alongside its two
children (`type`, `u`, `v` — 12 bytes).

[../eager-nil](../eager-nil/) is the same evaluator with the tag dropped and the
arity recovered from null children instead: a third less memory, at parity on
time. Its README has the measurements.

The tag is dispatched on with a `br_table`, which Cranelift compiles badly:
under wasmtime that alone costs ~2.7x on recursive fib (n=26: 2.05s, against
0.75s for the same 12-byte nodes dispatched with an `if` chain). Node/V8 is
indifferent. Nothing here depends on the `br_table`, so this is worth revisiting
if wasmtime is the target.

## Depth

Reduction, parsing and printing all recurse on the machine stack, so workload
depth is capped by whatever stack the engine grants (under Node, roughly
10⁴–10⁵ frames). [../eager-nil-vm](../eager-nil-vm/) runs the same reduction
with its frames in linear memory instead, when that cap is the problem.
