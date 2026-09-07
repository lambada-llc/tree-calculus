/-!
# Tree calculus: terms and reduction

Formalization of *triage calculus*, the reduction rules used throughout this
repository (see `reduction-rules/README.md`):

```
△  △        y  z          ⟶  y            (1)
△ (△ x)     y  z          ⟶  x z (y z)    (2)
△ (△ w x)   y  △          ⟶  w            (3a)
△ (△ w x)   y (△ u)       ⟶  x u          (3b)
△ (△ w x)   y (△ u v)     ⟶  y u v        (3c)
```

We use the *explicit application* representation: a term is either the node
operator `△` or an application.  Values (irreducible terms) are the binary
trees: leaves, stems and forks whose children are again values.
-/

namespace TreeCalculus

/-- Terms of tree calculus in the explicit-application representation:
the node operator `△` and binary application. -/
inductive Term : Type
  | leaf : Term
  | app : Term → Term → Term
deriving DecidableEq, Repr

namespace Term

/-- The node operator. -/
notation "△" => Term.leaf

/-- Application (left-associative, like juxtaposition on paper). -/
infixl:70 " ⬝ " => Term.app

/-- Values are the fully reduced terms: binary trees.  A value is a leaf, a
stem (one child) or a fork (two children), with children again values. -/
inductive IsValue : Term → Prop
  | leaf : IsValue △
  | stem {x : Term} : IsValue x → IsValue (△ ⬝ x)
  | fork {x y : Term} : IsValue x → IsValue y → IsValue (△ ⬝ x ⬝ y)

/-- Root contractions: the five reduction rules of triage calculus, applied
at the root of a term. -/
inductive Root : Term → Term → Prop
  /-- Rule (1): `△ △ y z ⟶ y` -/
  | k {y z : Term} : Root (△ ⬝ △ ⬝ y ⬝ z) y
  /-- Rule (2): `△ (△ x) y z ⟶ x z (y z)` -/
  | s {x y z : Term} : Root (△ ⬝ (△ ⬝ x) ⬝ y ⬝ z) (x ⬝ z ⬝ (y ⬝ z))
  /-- Rule (3a): `△ (△ w x) y △ ⟶ w` -/
  | fLeaf {w x y : Term} : Root (△ ⬝ (△ ⬝ w ⬝ x) ⬝ y ⬝ △) w
  /-- Rule (3b): `△ (△ w x) y (△ u) ⟶ x u` -/
  | fStem {w x y u : Term} : Root (△ ⬝ (△ ⬝ w ⬝ x) ⬝ y ⬝ (△ ⬝ u)) (x ⬝ u)
  /-- Rule (3c): `△ (△ w x) y (△ u v) ⟶ y u v` -/
  | fFork {w x y u v : Term} : Root (△ ⬝ (△ ⬝ w ⬝ x) ⬝ y ⬝ (△ ⬝ u ⬝ v)) (y ⬝ u ⬝ v)

/-- One-step reduction: a root contraction anywhere inside the term.
This is the full (unrestricted) rewrite relation — no strategy. -/
inductive Step : Term → Term → Prop
  | root {s u : Term} : Root s u → Step s u
  | appL {s s' t : Term} : Step s s' → Step (s ⬝ t) (s' ⬝ t)
  | appR {s t t' : Term} : Step t t' → Step (s ⬝ t) (s ⬝ t')

/-- Reflexive-transitive closure of `Step`. -/
inductive Steps : Term → Term → Prop
  | refl (t : Term) : Steps t t
  | head {s t u : Term} : Step s t → Steps t u → Steps s u

theorem Steps.single {s t : Term} (h : Step s t) : Steps s t :=
  .head h (.refl t)

theorem Steps.trans {s t u : Term} (h₁ : Steps s t) (h₂ : Steps t u) : Steps s u := by
  induction h₁ with
  | refl => exact h₂
  | head hst _ ih => exact .head hst (ih h₂)

theorem Steps.appL {s s' t : Term} (h : Steps s s') : Steps (s ⬝ t) (s' ⬝ t) := by
  induction h with
  | refl => exact .refl _
  | head hst _ ih => exact .head (.appL hst) ih

theorem Steps.appR {s t t' : Term} (h : Steps t t') : Steps (s ⬝ t) (s ⬝ t') := by
  induction h with
  | refl => exact .refl _
  | head hst _ ih => exact .head (.appR hst) ih

theorem Steps.app {s s' t t' : Term} (hs : Steps s s') (ht : Steps t t') :
    Steps (s ⬝ t) (s' ⬝ t') :=
  (hs.appL).trans (Steps.appR ht)

/-- A term is strongly normalizing when every `Step` chain starting from it is
finite, i.e. it is accessible for the reversed step relation. -/
def SN (t : Term) : Prop := Acc (flip Step) t

/-- `△` makes no step at all. -/
theorem leaf_no_step {u : Term} (h : Step △ u) : False := by
  cases h with
  | root hr => cases hr

theorem sn_leaf : SN △ := by
  constructor
  intro y hy
  exact absurd hy leaf_no_step

/-- Values make no step: values are exactly the normal forms. -/
theorem IsValue.no_step {v u : Term} (hv : IsValue v) (h : Step v u) : False := by
  induction hv generalizing u with
  | leaf => exact leaf_no_step h
  | stem _ ih =>
    cases h with
    | root hr => cases hr
    | appL h' => exact leaf_no_step h'
    | appR h' => exact ih h'
  | fork _ _ ihx ihy =>
    cases h with
    | root hr => cases hr
    | appL h' =>
      cases h' with
      | root hr => cases hr
      | appL h'' => exact leaf_no_step h''
      | appR h'' => exact ihx h''
    | appR h' => exact ihy h'

/-- Values are (trivially) strongly normalizing. -/
theorem IsValue.sn {v : Term} (hv : IsValue v) : SN v := by
  constructor
  intro y hy
  exact absurd hy hv.no_step

/-- Left subterms of strongly normalizing terms are strongly normalizing. -/
theorem sn_app_left {s t : Term} (h : SN (s ⬝ t)) : SN s := by
  suffices aux : ∀ x, Acc (flip Step) x → ∀ s t : Term, x = s ⬝ t → SN s from
    aux _ h s t rfl
  intro x hx
  induction hx with
  | intro x _ ih =>
    rintro s t rfl
    exact Acc.intro _ fun s₁ h₁ => ih (s₁ ⬝ t) (Step.appL h₁) s₁ t rfl

/-- Right subterms of strongly normalizing terms are strongly normalizing. -/
theorem sn_app_right {s t : Term} (h : SN (s ⬝ t)) : SN t := by
  suffices aux : ∀ x, Acc (flip Step) x → ∀ s t : Term, x = s ⬝ t → SN t from
    aux _ h s t rfl
  intro x hx
  induction hx with
  | intro x _ ih =>
    rintro s t rfl
    exact Acc.intro _ fun t₁ h₁ => ih (s ⬝ t₁) (Step.appR h₁) s t₁ rfl

/-- The usual reading of strong normalization: there is no infinite reduction
sequence out of a strongly normalizing term. -/
theorem SN.no_infinite_chain {t : Term} (h : SN t) :
    ¬∃ f : Nat → Term, f 0 = t ∧ ∀ n, Step (f n) (f (n + 1)) := by
  suffices aux : ∀ x, Acc (flip Step) x →
      ∀ f : Nat → Term, f 0 = x → (∀ n, Step (f n) (f (n + 1))) → False by
    rintro ⟨f, h0, hf⟩
    exact aux t h f h0 hf
  intro x hx
  induction hx with
  | intro x _ ih =>
    rintro f rfl hf
    exact ih (f 1) (hf 0) (fun n => f (n + 1)) rfl (fun n => hf (n + 1))

end Term

end TreeCalculus
