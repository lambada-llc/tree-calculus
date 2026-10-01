import TreeCalculus.Eval

/-!
# Evaluating over values only

`Term` represents values and redexes alike, as `△` and application, so a fork
is two `app` cells and every rule matches through nested applications. Eager
evaluation never builds a redex, though: its inputs and outputs are values.
`Tree` holds only those (plus `stuck`, for running out of fuel), one cell per
node, and `applyT` is `applyS` over it.

`applyT_toTerm` says the two agree exactly, through the embedding `toTerm`,
so `applyT` inherits `applyS`'s soundness.
-/

namespace TreeCalculus

/-- A value (leaf, stem or fork), or `stuck`: out of fuel. -/
inductive Tree : Type
  | leaf : Tree
  | stem : Tree → Tree
  | fork : Tree → Tree → Tree
  | stuck : Tree
deriving DecidableEq, Repr

namespace Tree

open Term

/-- The embedding into terms. -/
def toTerm : Tree → Term
  | leaf => △
  | stem x => △ ⬝ x.toTerm
  | fork x y => △ ⬝ x.toTerm ⬝ y.toTerm
  | stuck => Term.stuck

/-- `applyS` over `Tree`: rule (1) through (3c) by constructor, `stuck`
absorbing exactly as there. -/
def applyT : Nat → Tree → Tree → Tree
  | 0, _, _ => stuck
  | _ + 1, leaf, b => stem b
  | _ + 1, stem x, b => fork x b
  | _ + 1, fork leaf y, _ => y
  | n + 1, fork (stem x) y, z =>
      match applyT n x z with
      | stuck => stuck
      | xz => match applyT n y z with
        | stuck => stuck
        | yz => applyT n xz yz
  | _ + 1, fork (fork w _) _, leaf => w
  | n + 1, fork (fork _ x) _, stem u => applyT n x u
  | n + 1, fork (fork _ _) y, fork u v =>
      match applyT n y u with
      | stuck => stuck
      | yu => applyT n yu v
  | _ + 1, _, _ => stuck

theorem isStuck_toTerm {t : Tree} (h : t ≠ stuck) : isStuck t.toTerm = false := by
  cases t <;> first | rfl | exact absurd rfl h

/-- `applyT` computes `applyS`, on any inputs. -/
theorem applyT_toTerm (n : Nat) (a b : Tree) :
    (applyT n a b).toTerm = applyS n a.toTerm b.toTerm := by
  fun_induction applyT n a b with
  | case1 | case2 | case3 | case4 | case8 => rfl
  | case9 => rename_i ih; exact ih
  | case5 =>
    rename_i hx ihx
    simp only [toTerm, applyS]; rw [← ihx, hx]; rfl
  | case6 =>
    rename_i hy hx ihx ihy
    simp only [toTerm, applyS]; rw [← ihx, ← ihy, hy, isStuck_toTerm hx]; rfl
  | case7 =>
    rename_i hx hy ihx ihy ih
    simp only [toTerm, applyS]
    rw [← ihx, ← ihy, ← ih, isStuck_toTerm hx, isStuck_toTerm hy]; rfl
  | case10 =>
    rename_i hy ihy
    simp only [toTerm, applyS]; rw [← ihy, hy]; rfl
  | case11 =>
    rename_i hy ihy ih
    simp only [toTerm, applyS]; rw [← ihy, ← ih, isStuck_toTerm hy]; rfl
  | case12 =>
    rename_i a b h1 h2 h3 h4 h5 h6 h7
    cases a with
    | leaf => exact (h1 rfl).elim
    | stem x => exact (h2 x rfl).elim
    | stuck => rfl
    | fork p y =>
      cases p with
      | leaf => exact (h3 y rfl).elim
      | stem x => exact (h4 x y rfl).elim
      | stuck => rfl
      | fork w x =>
        cases b with
        | leaf => exact (h5 w x y rfl rfl).elim
        | stem u => exact (h6 w x y u rfl rfl).elim
        | fork u v => exact (h7 w x y u v rfl rfl).elim
        | stuck => rfl

/-- Soundness: whenever `applyT` does not run out of fuel on values, the
big-step relation holds. -/
theorem applyT_sound {n : Nat} {a b : Tree} (ha : IsValue a.toTerm)
    (hb : IsValue b.toTerm) (h : applyT n a b ≠ stuck) :
    Apply a.toTerm b.toTerm (applyT n a b).toTerm := by
  rw [applyT_toTerm]
  exact applyS_sound ha hb (by rw [← applyT_toTerm]; exact isStuck_toTerm h)

end Tree

end TreeCalculus
