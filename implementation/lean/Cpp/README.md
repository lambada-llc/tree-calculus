# Cpp: the C++ runtime's jets

`Jets.lean` checks the trees a jet of the C++ runtime
([`eager-graph-nil-mmap-32.hpp`](../../cpp/eager-graph-nil-mmap-32.hpp)) is keyed on, with
`decide +kernel` (never `native_decide`):

```lean
theorem skipLine_check : checkDropThrough skipLine [newline] 1000 = true  -- lambada's `skip_line`
```

so `DropJet.reaches skipLine_check` makes `DropJet skipLine [newline]` a `J` of `runtime_sound`:
`runtimeJets_sound`.

## Not proven yet: `divmod`

The runtime also answers arboretum's `Nat.divmod__fastest` natively (`div__fastest` and
`mod__fastest` reduce to it), and nothing here covers that jet yet. `divmod` is
`S (K (△U)) (S (K (△V)) K)`, so `divmod a` is `△U (△V (△△a))` by S and K alone, and the jet
answers `apply(△U (△V (△△a)), b)` with `△ q r`, `a = q·b + r` and `r < b`, when `a` and `b` are
naturals without trailing `△`s and `b` is not 0. The proof it needs:

- The partial's shape for every `a`, which `explore` can decide with `a` as the `var 0` no branch
  inspects; and a bridge between bit lists and `Nat` (core Lean, no Mathlib).
- Induction over `a`'s bits through `List.foldr__fastest`, keeping `a`'s top bits `= q·b + r` with
  `r < b`, from lemmas for `is_lte__fastest`, `sub__fastest`, `succ__fastest`, `double` and
  `canonicalize`, each by list induction; `explore` has to learn frames left pending and more than
  one opaque variable for those.
- The transition `reduce (△U (△V (△△a))) b k → dispatch (△ q r) k` as a `J` of `runtime_sound`,
  as `DropJet`'s are.

## Trusted, not proven

- `apply` is `step`: each case of `step` is the branch of `apply` with the same comment, each
  `Frame` the frame of the same name.
- Its memo is `RStep`'s: a `recall` hit is `hit`; `recall` pushing `MEMOIZE(a, b)` is `miss`;
  `memo_put` when that frame pops is `put`; an overwrite, `MEMO_MIN_STEPS` and a collection's
  filter are `forget`; a `reduce` without a lookup is `step`. A jet recording its answer for the
  frame `recall` pushed (`answered`) is `put`.
- Hash-consing is exact: equal indices iff equal trees.
- The collector frees only unreachable nodes, moves none, and drops every memo entry on a freed
  one.
- `skip_line`'s jet fires on exactly the tree `Jets/Trees.lean` defines — `intern_dag` reads
  [`jets.hpp`](../../cpp/jets.hpp)'s copy of `trees/*.dag` as `dag2lean.mjs` does, and `test.sh`
  checks both are what `trees/embed.mjs` writes — and takes only `DropJet` transitions.
- Lean's kernel.

## Regenerating the trees

From the repository root, `$LAMBADA` a lambada checkout (236cc8f for the committed trees) and
`$ARBORETUM` a built arboretum one (e3df589):

```
node bin/main.js -nat 10 -dag > implementation/lean/Cpp/trees/newline.dag
node bin/dag.js extract --symbol Lambada.skip_line $LAMBADA/compiler/compile_file.dag |
  node bin/dag.js eval --format dag > implementation/lean/Cpp/trees/skipLine.dag
node bin/dag.js extract --symbol Nat.divmod__fastest $ARBORETUM/src/.dag-bundle-canonical |
  node bin/dag.js eval --format dag > implementation/lean/Cpp/trees/divmod.dag
node implementation/lean/Cpp/trees/embed.mjs  # Jets/Trees.lean, jets.hpp
```
