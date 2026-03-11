A minimal reference implementation of [triage calculus](https://treecalcul.us/specification/) written directly in WebAssembly Text format (WAT).

Reads one [minimalist-binary-encoded](../../../conventions/README.md#minimalist-binary) trees from stdin, left-folds application, and writes the result to stdout.

## Usage

Use any WASI-compatible runtime:

```sh
# Prints 1 because 001010111 is the identity tree
{ echo 001010111; echo 1; } | wasmtime main.wasm
{ echo 001010111; echo 1; } | node main.mjs
```

Further examples:

```sh
# the function (λa.λb.a) has encoding 011:
{ echo 011; echo 1; echo 00111; } | wasmtime main.wasm # 1
{ echo 011; echo 00111; echo 1; } | wasmtime main.wasm # 00111

# the function (λa.λb.b) has encoding 0011001010111:
{ echo 0011001010111; echo 1; echo 00111; } | wasmtime main.wasm # 00111
{ echo 0011001010111; echo 00111; echo 1; } | wasmtime main.wasm # 1

## Build

Requires [wabt](https://github.com/WebAssembly/wabt) (`wat2wasm`):

```sh
brew install wabt # macOS
apt install wabt  # Debian/Ubuntu
```

Then:

```sh
wat2wasm main.wat -o main.wasm
```
