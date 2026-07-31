The minimal variant, and the only one that speaks
[minimalist binary](../../../conventions/README.md#minimalist-binary) rather than
ternary. See [../README.md](../README.md) for what all the WASM implementations
share: the I/O model, usage, build and test.

Being the smallest implementation is the point of this one, so it carries no
Node runner and no checked-in `main.wasm` — just the `main.wat` to build from.
That also means [../test.mjs](../test.mjs), which picks up variants by their
`main.mjs`, does not cover it.
