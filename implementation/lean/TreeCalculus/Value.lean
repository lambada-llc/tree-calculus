import TreeCalculus.Eval

/-!
# Evaluating over values, without `stuck`

`Tree` adds `stuck` to the values so that running out of fuel can be reported
in-band. `Value` is the values alone, leaf, stem and fork, so every inhabitant
is one, and running out of fuel goes through `Option` instead: `applyWithFuel`
is `Term.applyWithFuel` over it, completing the matrix of representation
(`Term`, `Tree`, `Value`) against out-of-fuel signal (`Option`, `stuck`).

`applyWithFuel_toTerm` says the two agree exactly, through the embedding
`toTerm`, so `applyWithFuel` inherits `Term.applyWithFuel`'s soundness.
-/

namespace TreeCalculus

/-- A value: leaf, stem or fork. -/
inductive Value : Type
  | leaf : Value
  | stem : Value → Value
  | fork : Value → Value → Value
deriving DecidableEq, Repr

namespace Value

open Term

/-- The embedding into terms. -/
def toTerm : Value → Term
  | leaf => △
  | stem x => △ ⬝ x.toTerm
  | fork x y => △ ⬝ x.toTerm ⬝ y.toTerm

/-- `Term.applyWithFuel` over `Value`: rule (1) through (3c) by constructor. -/
def applyWithFuel : Nat → Value → Value → Option Value
  | _, leaf, b => some (stem b)
  | _, stem x, b => some (fork x b)
  | _ + 1, fork leaf y, _ => some y
  | n + 1, fork (stem x) y, z => do
      let xz ← applyWithFuel n x z
      let yz ← applyWithFuel n y z
      applyWithFuel n xz yz
  | _ + 1, fork (fork w _) _, leaf => some w
  | n + 1, fork (fork _ x) _, stem u => applyWithFuel n x u
  | n + 1, fork (fork _ _) y, fork u v => do
      let yu ← applyWithFuel n y u
      applyWithFuel n yu v
  | 0, fork _ _, _ => none

/-- `applyWithFuel` computes `Term.applyWithFuel`, on any inputs. -/
theorem applyWithFuel_toTerm (n : Nat) (a b : Value) :
    (applyWithFuel n a b).map toTerm = Term.applyWithFuel n a.toTerm b.toTerm := by
  fun_induction applyWithFuel n a b with
  | case1 | case2 | case3 | case5 => cases ‹Nat› <;> rfl
  | case4 n x y z ihx ihy ih =>
    simp only [toTerm, Term.applyWithFuel, Option.bind_eq_bind, ← ihx, ← ihy]
    cases applyWithFuel n x z <;> cases applyWithFuel n y z <;> simp [← ih]
  | case6 n w x y u ih => simp only [toTerm, Term.applyWithFuel, ← ih]
  | case7 n w x y u v ihy ih =>
    simp only [toTerm, Term.applyWithFuel, Option.bind_eq_bind, ← ihy]
    cases applyWithFuel n y u <;> simp [← ih]
  | case8 p y b =>
    cases p <;> cases b <;> rfl

/-- Soundness: whenever `applyWithFuel` returns a result, the big-step relation
holds. -/
theorem applyWithFuel_sound {n : Nat} {a b v : Value} (h : applyWithFuel n a b = some v) :
    Apply a.toTerm b.toTerm v.toTerm :=
  Term.applyWithFuel_sound (by rw [← applyWithFuel_toTerm, h]; rfl)

end Value

end TreeCalculus
