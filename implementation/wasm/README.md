Reference implementations of [triage calculus](https://treecalcul.us/specification/) written directly in WebAssembly Text format (WAT).

The reduction rules are the same in all of them. What varies is how a tree is
encoded on the way in and out, and how a node is laid out in memory:

| variant | encoding | node |
| --- | --- | --- |
| [eager-value](./eager-value/) | [ternary](../../conventions/README.md#ternary) | arity-tagged (`type`, `u`, `v`) — 12 bytes |
| [eager-nil](./eager-nil/) | [ternary](../../conventions/README.md#ternary) | tagless, arity from null children — 8 bytes |
| [min-bin](./min-bin/) | [minimalist binary](../../conventions/README.md#minimalist-binary) | arity-tagged |

Each variant's own README covers only what is particular to it. Everything
below applies to all of them.

## I/O

Each reads one encoded tree per line of stdin, left-folds application over them,
and writes the result to stdout in the same encoding. The first line is
therefore the program and the rest are its arguments.

## Usage

Any WASI-compatible runtime. The `eager-*` variants also ship a `main.mjs` that
runs the same module under Node.

```sh
# 21100 is the identity tree, so this prints 10 back
cd eager-value
{ echo 21100; echo 10; } | wasmtime main.wasm
{ echo 21100; echo 10; } | node main.mjs

# the same in minimalist binary, where 001010111 is identity — prints 1
cd ../min-bin
{ echo 001010111; echo 1; } | wasmtime main.wasm
```

Further examples, given in both encodings. A leaf is `0` in ternary and `1` in
binary; a two-leaf fork is `200` and `00111`:

```sh
# (λa.λb.a) — ternary 10, binary 011 — keeps the first argument
{ echo 10;  echo 0;   echo 200; } | wasmtime main.wasm   # 0
{ echo 10;  echo 200; echo 0;   } | wasmtime main.wasm   # 200
{ echo 011; echo 1;     echo 00111; } | wasmtime main.wasm   # 1
{ echo 011; echo 00111; echo 1;     } | wasmtime main.wasm   # 00111

# (λa.λb.b) — ternary 2021100, binary 0011001010111 — keeps the second
{ echo 2021100; echo 0; echo 200; } | wasmtime main.wasm   # 200
{ echo 0011001010111; echo 1; echo 00111; } | wasmtime main.wasm   # 00111
```

A result is itself a tree, so reading it as data means converting it. Using the
"size" tree from the front page of [treecalcul.us](https://treecalcul.us/), whose
result encodes a natural number:

```sh
echo 212121201121211002110010202120212011201120212120112121100211001020212021201221000212011222011020112010010212011212011212110021100101021212001211002121202121202120002120102120002010212011202120212000101120212021200010211002120112120112121100211001010200 \
    > /tmp/size.ternary
# ../../../bin/main.js -ternary - -nat turns the resulting tree into a number.
{ cat /tmp/size.ternary; echo 0; }   | wasmtime main.wasm | ../../../bin/main.js -ternary - -nat # 1, a lonely leaf
{ cat /tmp/size.ternary; echo 10; }  | wasmtime main.wasm | ../../../bin/main.js -ternary - -nat # 2, a stem
{ cat /tmp/size.ternary; echo 200; } | wasmtime main.wasm | ../../../bin/main.js -ternary - -nat # 3, a fork
{ cat /tmp/size.ternary; cat /tmp/size.ternary; } | wasmtime main.wasm | ../../../bin/main.js -ternary - -nat
```

## Build

Requires [wabt](https://github.com/WebAssembly/wabt) (`wat2wasm`):

```sh
brew install wabt # macOS
apt install wabt  # Debian/Ubuntu
```

Then, in a variant's directory:

```sh
wat2wasm main.wat -o main.wasm
```

## Test

[test.mjs](./test.mjs) checks each variant against the JS reference
implementation:

```sh
node test.mjs
```

It picks up every sibling directory that ships a `main.mjs`, so a new ternary
variant only has to exist to be covered. `min-bin` ships only its `main.wat` and
is not covered.
