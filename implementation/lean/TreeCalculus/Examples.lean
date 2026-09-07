import TreeCalculus.StrongNormalization

/-!
# Examples and sanity checks

Exercises every reduction rule through the executable evaluator `evalF`, and
shows how the main theorem turns a successful evaluator run into a strong
normalization certificate.
-/

namespace TreeCalculus

namespace Term

/-- `K = △ △`: discards its second argument (`K y z ⟶ y` by rule (1)). -/
def K : Term := △ ⬝ △

/-- The identity program `△ (△ (△ △)) △` (see `conventions/README.md`). -/
def I : Term := △ ⬝ (△ ⬝ (△ ⬝ △)) ⬝ △

/-- `M z ⟶ z z`; hence `M ⬝ M` is the classic diverging self-application. -/
def M : Term := △ ⬝ (△ ⬝ I) ⬝ I

-- Rule (1): `△ △ y z ⟶ y`.
#guard evalF 10 (△ ⬝ △ ⬝ △ ⬝ (△ ⬝ △)) = some △

-- Rule (2) drives the identity program: `I x ⟶ ⋯ ⟶ x`.
#guard evalF 10 (I ⬝ △) = some △
#guard evalF 10 (I ⬝ K) = some K
#guard evalF 20 (I ⬝ I) = some I

-- Rule (3a): triage on a leaf.
#guard evalF 10 (△ ⬝ (△ ⬝ △ ⬝ (△ ⬝ △)) ⬝ △ ⬝ △) = some △

-- Rule (3b): triage on a stem, `△ (△ w x) y (△ u) ⟶ x u`.
#guard evalF 10 (△ ⬝ (△ ⬝ △ ⬝ K) ⬝ △ ⬝ (△ ⬝ △)) = some (△ ⬝ △ ⬝ △)

-- Rule (3c): triage on a fork, `△ (△ w x) y (△ u v) ⟶ y u v`.
#guard evalF 10 (△ ⬝ (△ ⬝ △ ⬝ △) ⬝ K ⬝ (△ ⬝ △ ⬝ △)) = some △

-- `M ⬝ M ⟶ I M (I M) ⟶ ⋯ ⟶ M M ⟶ ⋯` diverges; no fuel is ever enough.
#guard evalF 100 (M ⬝ M) = none

/-- A strong normalization certificate straight from an evaluator run:
`evalF` terminates on `I ⬝ I`, therefore *no* reduction strategy can diverge
on it. -/
example : SN (I ⬝ I) := sn_of_evalF (n := 20) (v := I) (by decide)

example : SN (△ ⬝ (△ ⬝ △ ⬝ △) ⬝ K ⬝ (△ ⬝ △ ⬝ △)) :=
  sn_of_evalF (n := 10) (v := △) (by decide)

end Term

end TreeCalculus
