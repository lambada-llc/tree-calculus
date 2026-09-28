/-!
# Trees, and the reduction rules they are reduced by

A value is a binary tree of △ nodes with at most two children (`leaf` △, `stem` △u,
`fork` △uv) — exactly the three node shapes of the nil-packed arena
(`implementation/cpp/eager-graph-nil-mmap-32.hpp`: `{0, 0}`, `{u, 0}`, `{u, v}`).
Application is not a node: `Eval a b r` says that applying value `a` to value `b`
reduces to value `r`, by the triage-calculus rules of `reduction-rules/README.md`,
read big-step and eagerly (both operands are values, which is the eager runtime's
contract: `apply(a, b)` is handed normal forms and returns one).
-/

namespace TreeCalculus

inductive Tree where
  | leaf
  | stem (u : Tree)
  | fork (u v : Tree)
  deriving DecidableEq, Repr

/-- `conventions/README.md`: false and true are the two smallest trees, △ and △△. -/
def Tree.false : Tree := .leaf
def Tree.true : Tree := .stem .leaf

/-- `apply(a, b) ⇓ r`. Rules (1)–(3c) are `reduction-rules/README.md`'s; `leaf` and `stem`
are what implicit application means for a node with fewer than three children: it grows. -/
inductive Eval : Tree → Tree → Tree → Prop
  | leaf : Eval .leaf b (.stem b)
  | stem : Eval (.stem u) b (.fork u b)
  /-- (1) △ △ y z ⟶ y -/
  | k : Eval (.fork .leaf y) z y
  /-- (2) △ (△ x) y z ⟶ x z (y z) -/
  | s : Eval y z yz → Eval x z xz → Eval xz yz r → Eval (.fork (.stem x) y) z r
  /-- (3a) △ (△ w x) y △ ⟶ w -/
  | triageLeaf : Eval (.fork (.fork w x) y) .leaf w
  /-- (3b) △ (△ w x) y (△ u) ⟶ x u -/
  | triageStem : Eval x u r → Eval (.fork (.fork w x) y) (.stem u) r
  /-- (3c) △ (△ w x) y (△ u v) ⟶ y u v -/
  | triageFork : Eval y u yu → Eval yu v r → Eval (.fork (.fork w x) y) (.fork u v) r

end TreeCalculus
