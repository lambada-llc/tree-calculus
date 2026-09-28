import TreeCalculus.Tree

/-! Pins down what `Symbolic.lean` relies on: the kernel compares shared trees in time
proportional to their DAG, not their expansion, because it caches every `whnf` by term. -/

namespace TreeCalculus.Tests

/-! Trees laid out the way `dag2lean.mjs` writes them, one definition a node, n + 1 of them for
2^n leaves: `x n` and `y n` are the same tree defined twice, and `w n` is `y n` with its last
leaf a stem instead — shared all the way down, and different only where a comparison looks
last. -/
open Lean in
local macro "towers" : command => do
  let name (p : String) (i : Nat) : Ident := mkIdent (.mkSimple s!"{p}{i}")
  let mut defs := #[]
  for (p, l, r, bottom) in [("x", "x", "x", ← `(Tree.leaf)), ("y", "y", "y", ← `(Tree.leaf)),
      ("w", "y", "w", ← `(Tree.stem .leaf))] do
    defs := defs.push (← `(def $(name p 0) : Tree := $bottom))
    for i in [1:61] do
      defs := defs.push (← `(def $(name p i) : Tree := .fork $(name l (i - 1)) $(name r (i - 1))))
  return ⟨mkNullNode defs⟩
towers

/-- 2^61 - 1 nodes a side. -/
example : x60 = y60 := by decide +kernel
example : x60 ≠ w60 := by decide +kernel

end TreeCalculus.Tests
