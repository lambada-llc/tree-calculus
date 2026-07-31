The tagged-node variant. See [../README.md](../README.md) for what all the WASM
implementations share: the I/O model, usage, build and test.

## Node representation

Each node carries its arity explicitly, as a `type` field alongside its two
children (`type`, `u`, `v` — 12 bytes).

[../eager-nil](../eager-nil/) is the same evaluator with the tag dropped and the
arity recovered from null children instead: a third less memory, and faster under
wasmtime though not under Node/V8. Its README has the measurements.
