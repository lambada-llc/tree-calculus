import TreeCalculus.Basic

/-!
# Eager (call-by-value / innermost) evaluation

Big-step eager evaluation: to evaluate an application, first evaluate both
sides to values, then apply the resulting values to each other, reducing
every redex that appears in the process.  This is exactly what the eager
evaluators elsewhere in this repository do.

`Apply a b v` says: applying value `a` to value `b` eagerly evaluates to `v`.
`Eval t v` says: eager evaluation of `t` terminates with result `v`.

Both are inductive *derivations*; a term on which the eager evaluator loops
forever simply has no `Eval` derivation.  The fuel-based computable evaluator
`evalF` below is proven equivalent (`evalF_sound`, `eval_complete`).
-/

namespace TreeCalculus

namespace Term

/-- Eager application of two values.  The first two constructors build larger
values (no redex yet); the last five mirror reduction rules (1), (2),
(3a), (3b), (3c), recursively evaluating the redex's contractum. -/
inductive Apply : Term → Term → Term → Prop
  /-- `△ ⬝ b` is a value (stem): nothing to contract. -/
  | underLeaf {b : Term} : Apply △ b (△ ⬝ b)
  /-- `△ ⬝ x ⬝ b` is a value (fork): nothing to contract. -/
  | underStem {x b : Term} : Apply (△ ⬝ x) b (△ ⬝ x ⬝ b)
  /-- Rule (1): `△ △ y z ⟶ y`; `y` is already a value. -/
  | k {y z : Term} : Apply (△ ⬝ △ ⬝ y) z y
  /-- Rule (2): `△ (△ x) y z ⟶ x z (y z)`; evaluate the contractum eagerly. -/
  | s {x y z xz yz v : Term} :
      Apply x z xz → Apply y z yz → Apply xz yz v →
      Apply (△ ⬝ (△ ⬝ x) ⬝ y) z v
  /-- Rule (3a): `△ (△ w x) y △ ⟶ w`; `w` is already a value. -/
  | fLeaf {w x y : Term} : Apply (△ ⬝ (△ ⬝ w ⬝ x) ⬝ y) △ w
  /-- Rule (3b): `△ (△ w x) y (△ u) ⟶ x u`; evaluate the contractum eagerly. -/
  | fStem {w x y u v : Term} :
      Apply x u v → Apply (△ ⬝ (△ ⬝ w ⬝ x) ⬝ y) (△ ⬝ u) v
  /-- Rule (3c): `△ (△ w x) y (△ u v) ⟶ y u v`; evaluate the contractum eagerly. -/
  | fFork {w x y u v yu r : Term} :
      Apply y u yu → Apply yu v r →
      Apply (△ ⬝ (△ ⬝ w ⬝ x) ⬝ y) (△ ⬝ u ⬝ v) r

/-- Big-step eager evaluation: evaluate both sides of an application to
values first, then apply them.  `Eval t v` holds iff the eager evaluator
terminates on `t` with result `v`. -/
inductive Eval : Term → Term → Prop
  | leaf : Eval △ △
  | app {s t s' t' v : Term} :
      Eval s s' → Eval t t' → Apply s' t' v → Eval (s ⬝ t) v

/-! ## Inversion lemmas -/

theorem Eval.leaf_inv {v : Term} (h : Eval △ v) : v = △ := by
  cases h; rfl

theorem Eval.app_inv {s t v : Term} (h : Eval (s ⬝ t) v) :
    ∃ s' t', Eval s s' ∧ Eval t t' ∧ Apply s' t' v := by
  cases h with
  | app hs ht ha => exact ⟨_, _, hs, ht, ha⟩

/-- Evaluating a stem-shaped term `△ ⬝ x` evaluates `x` and rebuilds the stem. -/
theorem Eval.stem_inv {x v : Term} (h : Eval (△ ⬝ x) v) :
    ∃ x', Eval x x' ∧ v = △ ⬝ x' := by
  obtain ⟨e, x', he, hx, ha⟩ := h.app_inv
  cases he.leaf_inv
  cases ha
  exact ⟨x', hx, rfl⟩

/-- Evaluating a fork-shaped term `△ ⬝ p ⬝ y` evaluates `p` and `y` and
rebuilds the fork. -/
theorem Eval.fork_inv {p y v : Term} (h : Eval (△ ⬝ p ⬝ y) v) :
    ∃ p' y', Eval p p' ∧ Eval y y' ∧ v = △ ⬝ p' ⬝ y' := by
  obtain ⟨c, y', hc, hy, ha⟩ := h.app_inv
  obtain ⟨p', hp, rfl⟩ := hc.stem_inv
  cases ha
  exact ⟨p', y', hp, hy, rfl⟩

theorem Apply.k_inv {y z v : Term} (h : Apply (△ ⬝ △ ⬝ y) z v) : v = y := by
  cases h; rfl

theorem Apply.s_inv {x y z v : Term} (h : Apply (△ ⬝ (△ ⬝ x) ⬝ y) z v) :
    ∃ xz yz, Apply x z xz ∧ Apply y z yz ∧ Apply xz yz v := by
  cases h with
  | s h₁ h₂ h₃ => exact ⟨_, _, h₁, h₂, h₃⟩

theorem Apply.fLeaf_inv {w x y v : Term} (h : Apply (△ ⬝ (△ ⬝ w ⬝ x) ⬝ y) △ v) :
    v = w := by
  cases h; rfl

theorem Apply.fStem_inv {w x y u v : Term}
    (h : Apply (△ ⬝ (△ ⬝ w ⬝ x) ⬝ y) (△ ⬝ u) v) : Apply x u v := by
  cases h with
  | fStem h₁ => exact h₁

theorem Apply.fFork_inv {w x y u u' v : Term}
    (h : Apply (△ ⬝ (△ ⬝ w ⬝ x) ⬝ y) (△ ⬝ u ⬝ u') v) :
    ∃ yu, Apply y u yu ∧ Apply yu u' v := by
  cases h with
  | fFork h₁ h₂ => exact ⟨_, h₁, h₂⟩

/-! ## Sanity: eager evaluation produces values, along actual reductions -/

/-- Applying values yields values. -/
theorem Apply.isValue {a b v : Term} (h : Apply a b v)
    (ha : IsValue a) (hb : IsValue b) : IsValue v := by
  induction h with
  | underLeaf => exact .stem hb
  | underStem => cases ha with | stem hx => exact .fork hx hb
  | k => cases ha with | fork _ hy => exact hy
  | s _ _ _ ih₁ ih₂ ih₃ =>
    cases ha with | fork hx hy =>
    cases hx with | stem hx =>
    exact ih₃ (ih₁ hx hb) (ih₂ hy hb)
  | fLeaf =>
    cases ha with | fork hwx _ =>
    cases hwx with | fork hw _ => exact hw
  | fStem _ ih =>
    cases ha with | fork hwx _ =>
    cases hwx with | fork _ hx =>
    cases hb with | stem hu => exact ih hx hu
  | fFork _ _ ih₁ ih₂ =>
    cases ha with | fork _ hy =>
    cases hb with | fork hu hv =>
    exact ih₂ (ih₁ hy hu) hv

/-- Eager evaluation produces values. -/
theorem Eval.isValue {t v : Term} (h : Eval t v) : IsValue v := by
  induction h with
  | leaf => exact .leaf
  | app _ _ ha ihs iht => exact ha.isValue ihs iht

/-- Values evaluate to themselves. -/
theorem IsValue.eval_self {v : Term} (hv : IsValue v) : Eval v v := by
  induction hv with
  | leaf => exact .leaf
  | stem _ ih => exact .app .leaf ih .underLeaf
  | fork _ _ ihx ihy => exact .app (.app .leaf ihx .underLeaf) ihy .underStem

/-- Applying values is a genuine multi-step reduction of `a ⬝ b`. -/
theorem Apply.steps {a b v : Term} (h : Apply a b v) : Steps (a ⬝ b) v := by
  induction h with
  | underLeaf => exact .refl _
  | underStem => exact .refl _
  | k => exact .single (.root .k)
  | s _ _ _ ih₁ ih₂ ih₃ =>
    exact .head (.root .s) ((Steps.app ih₁ ih₂).trans ih₃)
  | fLeaf => exact .single (.root .fLeaf)
  | fStem _ ih => exact .head (.root .fStem) ih
  | fFork _ _ ih₁ ih₂ =>
    exact .head (.root .fFork) ((Steps.appL ih₁).trans ih₂)

/-- Eager evaluation is a genuine multi-step reduction: `Eval t v → t ⟶* v`. -/
theorem Eval.steps {t v : Term} (h : Eval t v) : Steps t v := by
  induction h with
  | leaf => exact .refl _
  | app _ _ ha ihs iht => exact (Steps.app ihs iht).trans ha.steps

/-! ## A computable, fuel-based eager evaluator

`applyF`/`evalF` implement the same strategy as executable functions.  Fuel
is consumed at each contraction; `none` means "out of fuel" (or, for
`applyF`, an argument that is not a value — which never happens when called
from `evalF`). -/

/-- Fuel-based eager application of two values. -/
def applyF : Nat → Term → Term → Option Term
  | _, .leaf, b => some (.app .leaf b)
  | _, .app .leaf x, b => some (.app (.app .leaf x) b)
  | _ + 1, .app (.app .leaf .leaf) y, _ => some y
  | n + 1, .app (.app .leaf (.app .leaf x)) y, z => do
      let xz ← applyF n x z
      let yz ← applyF n y z
      applyF n xz yz
  | _ + 1, .app (.app .leaf (.app (.app .leaf w) _)) _, .leaf => some w
  | n + 1, .app (.app .leaf (.app (.app .leaf _) x)) _, .app .leaf u =>
      applyF n x u
  | n + 1, .app (.app .leaf (.app (.app .leaf _) _)) y, .app (.app .leaf u) v => do
      let yu ← applyF n y u
      applyF n yu v
  | _, _, _ => none

/-- Fuel-based eager evaluation. -/
def evalF : Nat → Term → Option Term
  | _, .leaf => some .leaf
  | 0, .app _ _ => none
  | n + 1, .app s t => do
      let s' ← evalF n s
      let t' ← evalF n t
      applyF n s' t'

/-- Soundness: whenever the fuel-based applier returns a result, the big-step
relation holds. -/
theorem applyF_sound {fuel : Nat} {a b v : Term}
    (h : applyF fuel a b = some v) : Apply a b v := by
  fun_induction applyF fuel a b generalizing v with
  | case1 => exact (Option.some.inj h) ▸ .underLeaf
  | case2 => exact (Option.some.inj h) ▸ .underStem
  | case3 => exact (Option.some.inj h) ▸ .k
  | case4 n x y z ih₁ ih₂ ih₃ =>
    simp only [Option.bind_eq_bind, Option.bind_eq_some_iff] at h
    obtain ⟨xz, hxz, yz, hyz, hv⟩ := h
    exact .s (ih₁ hxz) (ih₂ hyz) (ih₃ _ _ hv)
  | case5 => exact (Option.some.inj h) ▸ .fLeaf
  | case6 n w x y u ih => exact .fStem (ih h)
  | case7 n w x y u v ih₁ ih₂ =>
    simp only [Option.bind_eq_bind, Option.bind_eq_some_iff] at h
    obtain ⟨yu, hyu, hv⟩ := h
    exact .fFork (ih₁ hyu) (ih₂ _ hv)
  | case8 => simp at h

/-- Soundness: whenever the fuel-based evaluator returns a result, the
big-step relation holds. -/
theorem evalF_sound {fuel : Nat} {t v : Term} (h : evalF fuel t = some v) :
    Eval t v := by
  fun_induction evalF fuel t generalizing v with
  | case1 => exact (Option.some.inj h) ▸ .leaf
  | case2 => simp at h
  | case3 n s t ih₁ ih₂ =>
    simp only [Option.bind_eq_bind, Option.bind_eq_some_iff] at h
    obtain ⟨s', hs, t', ht, hv⟩ := h
    exact .app (ih₁ hs) (ih₂ ht) (applyF_sound hv)

/-- More fuel never hurts. -/
theorem applyF_mono {fuel : Nat} {a b v : Term} (h : applyF fuel a b = some v) :
    applyF (fuel + 1) a b = some v := by
  fun_induction applyF fuel a b generalizing v with
  | case1 => simp only [applyF]; exact h
  | case2 => simp only [applyF]; exact h
  | case3 => simp only [applyF]; exact h
  | case4 n x y z ih₁ ih₂ ih₃ =>
    simp only [Option.bind_eq_bind, Option.bind_eq_some_iff] at h
    obtain ⟨xz, hxz, yz, hyz, hv⟩ := h
    simp only [applyF, Option.bind_eq_bind, Option.bind_eq_some_iff]
    exact ⟨xz, ih₁ hxz, yz, ih₂ hyz, ih₃ _ _ hv⟩
  | case5 => simp only [applyF]; exact h
  | case6 n w x y u ih => simp only [applyF]; exact ih h
  | case7 n w x y u v ih₁ ih₂ =>
    simp only [Option.bind_eq_bind, Option.bind_eq_some_iff] at h
    obtain ⟨yu, hyu, hv⟩ := h
    simp only [applyF, Option.bind_eq_bind, Option.bind_eq_some_iff]
    exact ⟨yu, ih₁ hyu, ih₂ _ hv⟩
  | case8 => exact Option.noConfusion h

theorem applyF_le {n m : Nat} (hle : n ≤ m) {a b v : Term}
    (h : applyF n a b = some v) : applyF m a b = some v := by
  induction hle with
  | refl => exact h
  | step _ ih => exact applyF_mono ih

/-- More fuel never hurts. -/
theorem evalF_mono {fuel : Nat} {t v : Term} (h : evalF fuel t = some v) :
    evalF (fuel + 1) t = some v := by
  fun_induction evalF fuel t generalizing v with
  | case1 => simp only [evalF]; exact h
  | case2 => exact Option.noConfusion h
  | case3 n s t ih₁ ih₂ =>
    simp only [Option.bind_eq_bind, Option.bind_eq_some_iff] at h
    obtain ⟨s', hs, t', ht, hv⟩ := h
    simp only [evalF, Option.bind_eq_bind, Option.bind_eq_some_iff]
    exact ⟨s', ih₁ hs, t', ih₂ ht, applyF_mono hv⟩

theorem evalF_le {n m : Nat} (hle : n ≤ m) {t v : Term}
    (h : evalF n t = some v) : evalF m t = some v := by
  induction hle with
  | refl => exact h
  | step _ ih => exact evalF_mono ih

/-- Completeness: every big-step application is computed by `applyF` with
enough fuel. -/
theorem Apply.applyF_complete {a b v : Term} (h : Apply a b v) :
    ∃ n, applyF n a b = some v := by
  induction h with
  | underLeaf => exact ⟨0, by simp [applyF]⟩
  | underStem => exact ⟨0, by simp [applyF]⟩
  | k => exact ⟨1, by simp [applyF]⟩
  | s _ _ _ ih₁ ih₂ ih₃ =>
    obtain ⟨n₁, h₁⟩ := ih₁
    obtain ⟨n₂, h₂⟩ := ih₂
    obtain ⟨n₃, h₃⟩ := ih₃
    refine ⟨max n₁ (max n₂ n₃) + 1, ?_⟩
    simp only [applyF, Option.bind_eq_bind, Option.bind_eq_some_iff]
    exact ⟨_, applyF_le (Nat.le_max_left _ _) h₁,
      _, applyF_le (Nat.le_trans (Nat.le_max_left _ _) (Nat.le_max_right _ _)) h₂,
      applyF_le (Nat.le_trans (Nat.le_max_right _ _) (Nat.le_max_right _ _)) h₃⟩
  | fLeaf => exact ⟨1, by simp [applyF]⟩
  | fStem _ ih =>
    obtain ⟨n, h⟩ := ih
    exact ⟨n + 1, by simpa only [applyF] using h⟩
  | fFork _ _ ih₁ ih₂ =>
    obtain ⟨n₁, h₁⟩ := ih₁
    obtain ⟨n₂, h₂⟩ := ih₂
    refine ⟨max n₁ n₂ + 1, ?_⟩
    simp only [applyF, Option.bind_eq_bind, Option.bind_eq_some_iff]
    exact ⟨_, applyF_le (Nat.le_max_left _ _) h₁,
      applyF_le (Nat.le_max_right _ _) h₂⟩

/-- Completeness: every big-step evaluation is computed by `evalF` with
enough fuel.  Together with `evalF_sound`, the relation `Eval` says exactly
"the eager evaluator terminates". -/
theorem Eval.evalF_complete {t v : Term} (h : Eval t v) :
    ∃ n, evalF n t = some v := by
  induction h with
  | leaf => exact ⟨0, rfl⟩
  | app _ _ ha ihs iht =>
    obtain ⟨n₁, h₁⟩ := ihs
    obtain ⟨n₂, h₂⟩ := iht
    obtain ⟨n₃, h₃⟩ := ha.applyF_complete
    refine ⟨max n₁ (max n₂ n₃) + 1, ?_⟩
    simp only [evalF, Option.bind_eq_bind, Option.bind_eq_some_iff]
    exact ⟨_, evalF_le (Nat.le_max_left _ _) h₁,
      _, evalF_le (Nat.le_trans (Nat.le_max_left _ _) (Nat.le_max_right _ _)) h₂,
      applyF_le (Nat.le_trans (Nat.le_max_right _ _) (Nat.le_max_right _ _)) h₃⟩

end Term

end TreeCalculus
