A reference implementation of [triage calculus](https://treecalcul.us/specification/) written directly in WebAssembly Text format (WAT).

Reads one [ternary-encoded](../../../conventions/README.md#ternary) tree per stdin line, left-folds application, and writes the result to stdout.

## Usage

Use any WASI-compatible runtime:

```sh
# Prints 10 because 21100 is the identity tree
{ echo 21100; echo 10; } | wasmtime main.wasm
{ echo 21100; echo 10; } | node main.mjs
```

Further examples:

```sh
# the function (λa.λb.a) has ternary encoding "10":
{ echo 10; echo 0; echo 200; } | wasmtime main.wasm # 0
{ echo 10; echo 200; echo 0; } | wasmtime main.wasm # 200

# the function (λa.λb.b) has ternary encoding "2021100":
{ echo 2021100; echo 0; echo 200; } | wasmtime main.wasm # 200
{ echo 2021100; echo 200; echo 0; } | wasmtime main.wasm # 0

# using the "size" tree, see front page example of https://treecalcul.us/
echo 212121201121211002110010202120212011201120212120112121100211001020212021201221000212011222011020112010010212011212011212110021100101021212001211002121202121202120002120102120002010212011202120212000101120212021200010211002120112120112121100211001010200 \
    > /tmp/size.ternary
# The result of "size" applied to some tree is a tree representing a natural number,
# so we use [../../bin/main.js -ternary - -nat] to convert that tree into a number we can read.
{ cat /tmp/size.ternary; echo 0; }   | wasmtime main.wasm | ../../bin/main.js -ternary - -nat # 1 (because "0" is the encoding of a lonely leaf node)
{ cat /tmp/size.ternary; echo 10; }  | wasmtime main.wasm | ../../bin/main.js -ternary - -nat # 2 (because "10" is the encoding of a simple "stem", a node with one child)
{ cat /tmp/size.ternary; echo 200; } | wasmtime main.wasm | ../../bin/main.js -ternary - -nat # 3 (because "200" is the encoding of a simple "fork", a node with two children)
{ cat /tmp/size.ternary; cat /tmp/size.ternary; } | wasmtime main.wasm | ../../bin/main.js -ternary - -nat
```

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
