# Cpp: the C++ runtime's jets

`Jets.lean` checks the trees a jet of the C++ runtime
([`eager-graph-nil-mmap-32.hpp`](../../cpp/eager-graph-nil-mmap-32.hpp)) is keyed on, with
`decide +kernel` (never `native_decide`):

```lean
theorem skipLine_check : checkDropThrough skipLine [newline] 1000 = true  -- lambada's `skip_line`
```

so `DropJet.reaches skipLine_check` makes `DropJet skipLine [newline]` a `J` of `runtime_sound`:
`runtimeJets_sound`.

## Not proven yet: `add` and `mul`

The runtime also answers arboretum's `Nat.add` and `Nat.mul` natively, and nothing here covers
those jets yet. Each answers `apply(P, b)` for `P` the partial of `a` — with `a + b` or `a · b` —
when `a` and `b` are naturals without trailing `△`s:

- `mul` is `S (K (△U)) K`, so `mul a` is `△U (△△a)` by S and K alone.
- `add a` is `△ (△ (△C Z(a))) K`, where the rules build `Z(a)` a layer per cell as `_zip_rest`
  takes `a` apart: `Z([])` is `N` and `Z(h:t)` is `△ L(h) (△ (△ (h:t) △) (△△ Z(t)))`. `C`, `K`, `N`,
  `L(△)` and `L(△△)` are read off `addPartial`, which `addPartial_check` shows is `add [△, △△]`,
  and the jet walks `Z(a)` down `a` to tell it apart.

The proof they need:

- The partials' shapes for every `a`: for `mul`, `explore` can decide it with `a` as the `var 0`
  no branch inspects; for `add`, by list induction over `a`. And a bridge between bit lists and
  `Nat` (core Lean, no Mathlib).
- `add`: induction over the bits `_zip_rest` pairs, through `_add`'s carry and `_carry`'s `succ`,
  each by list induction; `explore` has to learn frames left pending and more than one opaque
  variable for those. `mul`: induction over `a`'s bits through `List.foldr`, `s ↦ 2s + h·b`, from
  `add`'s lemma and one for `double`.
- The transitions `reduce P b k → dispatch r k` as `J`s of `runtime_sound`, as `DropJet`'s are.

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
for f in add mul; do
  node bin/dag.js extract --symbol Nat.$f $ARBORETUM/src/.dag-bundle-canonical |
    node bin/dag.js eval --format dag > implementation/lean/Cpp/trees/$f.dag
done
node bin/main.js -dag -file implementation/lean/Cpp/trees/add.dag -term '△ △ (△ (△ △) △)' -dag \
  > implementation/lean/Cpp/trees/addPartial.dag
node implementation/lean/Cpp/trees/embed.mjs  # Jets/Trees.lean, jets.hpp
```
