import TreeCalculus.StrongNormalization

/-!
# Examples and sanity checks

Exercises every reduction rule through the executable evaluator `evalWithFuel`,
and shows how the main theorem turns a successful evaluator run into a strong
normalization certificate.
-/

namespace TreeCalculus

namespace Term

/-- `const = △ △`: discards its second argument (`const y z ⟶ y` by rule (1)). -/
def const : Term := △ ⬝ △

/-- The identity program `△ (△ (△ △)) △` (see `conventions/README.md`). -/
def identity : Term := △ ⬝ (△ ⬝ (△ ⬝ △)) ⬝ △

/-- `selfApply z ⟶ z z`; hence `selfApply ⬝ selfApply` is the classic diverging
self-application. -/
def selfApply : Term := △ ⬝ (△ ⬝ identity) ⬝ identity

-- Rule (1): `△ △ y z ⟶ y`.
#guard evalWithFuel 10 (△ ⬝ △ ⬝ △ ⬝ (△ ⬝ △)) = some △

-- Rule (2) drives the identity program: `identity x ⟶ ⋯ ⟶ x`.
#guard evalWithFuel 10 (identity ⬝ △) = some △
#guard evalWithFuel 10 (identity ⬝ const) = some const
#guard evalWithFuel 20 (identity ⬝ identity) = some identity

-- Rule (3a): triage on a leaf.
#guard evalWithFuel 10 (△ ⬝ (△ ⬝ △ ⬝ (△ ⬝ △)) ⬝ △ ⬝ △) = some △

-- Rule (3b): triage on a stem, `△ (△ w x) y (△ u) ⟶ x u`.
#guard evalWithFuel 10 (△ ⬝ (△ ⬝ △ ⬝ const) ⬝ △ ⬝ (△ ⬝ △)) = some (△ ⬝ △ ⬝ △)

-- Rule (3c): triage on a fork, `△ (△ w x) y (△ u v) ⟶ y u v`.
#guard evalWithFuel 10 (△ ⬝ (△ ⬝ △ ⬝ △) ⬝ const ⬝ (△ ⬝ △ ⬝ △)) = some △

-- `selfApply ⬝ selfApply ⟶ identity selfApply (identity selfApply) ⟶ ⋯
--   ⟶ selfApply selfApply ⟶ ⋯` diverges; no fuel is ever enough.
#guard evalWithFuel 100 (selfApply ⬝ selfApply) = none

-- `applyOrStuck` agrees, reporting divergence as `stuck` rather than `none`.
#guard applyOrStuck 20 identity const = const
#guard applyOrStuck 100 selfApply selfApply = stuck

-- `△ (△ const) selfApply selfApply ⟶ const selfApply (selfApply selfApply)`:
-- rule (1) would discard the diverging `selfApply selfApply`, but eager
-- evaluation reduces it first. `stuck` survives the discard.
#guard evalWithFuel 100 (△ ⬝ (△ ⬝ const) ⬝ selfApply ⬝ selfApply) = none
#guard applyOrStuck 100 (△ ⬝ (△ ⬝ const) ⬝ selfApply) selfApply = stuck

/-- A strong normalization certificate straight from an evaluator run:
`evalWithFuel` terminates on `identity ⬝ identity`, therefore *no* reduction
strategy can diverge on it. -/
example : SN (identity ⬝ identity) := sn_of_evalWithFuel (n := 20) (v := identity) (by decide)

example : SN (△ ⬝ (△ ⬝ △ ⬝ △) ⬝ const ⬝ (△ ⬝ △ ⬝ △)) :=
  sn_of_evalWithFuel (n := 10) (v := △) (by decide)

end Term

end TreeCalculus
