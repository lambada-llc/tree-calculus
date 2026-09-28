import TreeCalculus.Tree

/-! Pins down what `Symbolic.lean` relies on: the kernel compares shared trees in time
proportional to their DAG, not their expansion, because it caches every `whnf` by term. -/

namespace TreeCalculus.Tests

/-- A tree with 2^n copies of `bottom`, as n + 1 distinct nodes. -/
def tower (bottom : Tree) : Nat → Tree
  | 0 => bottom
  | n + 1 => .fork (tower bottom n) (tower bottom n)

/-- Both sides built separately; the expansion has 2^61 - 1 nodes. -/
example : tower .leaf 60 = tower .leaf 60 := by decide +kernel
example : tower .leaf 60 ≠ tower (.stem .leaf) 60 := by decide +kernel

end TreeCalculus.Tests
