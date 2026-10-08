import Lean.Elab.Tactic.ElabTerm
import Lean.Meta.Tactic.AuxLemma

/-!
# `kernel_rfl`

`decide +kernel` evaluates a `Decidable` instance in the kernel. On `checkRule ρ ℓ` that ends in a
structural comparison of the run's last state with `ρ.rhs`, which walks every `lit` in it node by
node — the expanded tree, not the DAG. Stated as the equation `ssteps ℓ ρ.lhs = some ρ.rhs` and
closed by `Eq.refl`, the same run is a kernel *defeq* check instead, which sees that `lit t57` is
`lit t57` without unfolding `t57`. Like `decide +kernel`, the proof is checked by the kernel
alone (an auxiliary lemma), not by the elaborator's `isDefEq`, which runs out of recursion depth
on long rules.
-/

open Lean Elab Tactic Meta in
/-- Close `a = b` by `Eq.refl a`, checked by the kernel alone. -/
elab "kernel_rfl" : tactic => closeMainGoalUsing `kernel_rfl fun type _ => do
  let type ← instantiateMVars type
  let some (_, a, _) := type.eq? | throwError "kernel_rfl: the goal is not an equation"
  return mkConst (← withOptions (Elab.async.set · false) <| mkAuxLemma [] type (← mkEqRefl a))
