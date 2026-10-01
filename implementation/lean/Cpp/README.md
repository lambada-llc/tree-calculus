# Cpp: the C++ runtime's jets

`Jets.lean` checks the trees a jet of the C++ runtime
([`eager-graph-nil-mmap-32.hpp`](../../cpp/eager-graph-nil-mmap-32.hpp)) is keyed on, with
`decide +kernel` (never `native_decide`):

```lean
theorem skipLine_check : checkDropThrough skipLine [newline] 1000 = true  -- lambada's `skip_line`
```

so `DropJet.reaches skipLine_check` makes `DropJet skipLine [newline]` a `J` of `runtime_sound`.

## Trusted, not proven

- `apply` is `step`: each case of `step` is the branch of `apply` with the same comment, each
  `Frame` the frame of the same name.
- Its memo is `RStep`'s: a `recall` hit is `hit`; `recall` pushing `MEMOIZE(a, b)` is `miss`;
  `memo_put` when that frame pops is `put`; an overwrite, `MEMO_MIN_STEPS` and a collection's
  filter are `forget`; a `reduce` without a lookup is `step`.
- Hash-consing is exact: equal indices iff equal trees.
- The collector frees only unreachable nodes, moves none, and drops every memo entry on a freed
  one.
- A jet fires on exactly the tree `Jets/Trees.lean` defines, and takes only `DropJet`
  transitions.
- Lean's kernel.

## Regenerating the trees

From the repository root, `$LAMBADA` a lambada checkout (236cc8f for the committed trees):

```
node bin/main.js -nat 10 -dag > implementation/lean/Cpp/trees/newline.dag
node bin/dag.js extract --symbol Lambada.skip_line $LAMBADA/compiler/compile_file.dag |
  node bin/dag.js eval --format dag > implementation/lean/Cpp/trees/skipLine.dag
cd implementation/lean/Cpp && node dag2lean.mjs Cpp.Jets newline=trees/newline.dag skipLine=trees/skipLine.dag > Jets/Trees.lean
```
