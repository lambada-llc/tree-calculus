import TreeCalculus.Eval

/-!
# Eager termination implies strong normalization

Main result: if the eager evaluator terminates on a term, then that term is
strongly normalizing for the *full* reduction relation — every reduction
sequence, under any strategy whatsoever, is finite.

This is not automatic: eager ("innermost") termination implying strong
normalization is a property of *orthogonal* rewrite systems (the five triage
rules do not overlap, and each is left-linear); in general — e.g. in
λ-calculus, where `λx.Ω` is a call-by-value normal form but not strongly
normalizing — it fails.

Proof outline:

* `Eval.step_preserved`: a reduction step anywhere in a term preserves the
  result of eager evaluation.  This is where orthogonality does its work: a
  step is either inside a subterm that eager evaluation evaluates anyway, or
  it is a root contraction, which eager evaluation performs as well (in a
  different order).
* `sn_app_closure`: to prove `s ⬝ t` strongly normalizing it suffices to have
  `SN s`, `SN t` and that every *root* contraction of any reduct of `s ⬝ t`
  is strongly normalizing.  (Double well-founded induction on `SN s`/`SN t`.)
* `apply_sn`: the heart of the proof, by structural induction on the
  derivation of `Apply a b v`.  When a root contraction fires on a reduct
  `s' ⬝ t'`, the pattern of the fired rule is mirrored in the values `a`, `b`
  (redex patterns cannot be created or destroyed by reductions inside the
  variable positions), so the contractum is covered by the induction
  hypotheses for the sub-derivations of the `Apply` derivation.
* `Eval.sn`: induction on the evaluation derivation, using `apply_sn` at
  every application node.
-/

namespace TreeCalculus

namespace Term

/-! ## Shape lemmas for evaluation results

Eager evaluation of a leaf/stem/fork-shaped term returns a value of the same
shape, with the components evaluated.  We need both the positive
(decomposition) direction and refutations of the mismatched combinations. -/

theorem Eval.leaf_not_stem {x : Term} (h : Eval △ (△ ⬝ x)) : False :=
  Term.noConfusion h.leaf_inv

theorem Eval.leaf_not_fork {p y : Term} (h : Eval △ (△ ⬝ p ⬝ y)) : False :=
  Term.noConfusion h.leaf_inv

theorem Eval.stem_not_leaf {x : Term} (h : Eval (△ ⬝ x) △) : False := by
  obtain ⟨x', _, heq⟩ := h.stem_inv
  exact Term.noConfusion heq

theorem Eval.stem_not_fork {x p y : Term} (h : Eval (△ ⬝ x) (△ ⬝ p ⬝ y)) :
    False := by
  obtain ⟨x', _, heq⟩ := h.stem_inv
  injection heq with h₁ _
  exact Term.noConfusion h₁

theorem Eval.fork_not_leaf {p y : Term} (h : Eval (△ ⬝ p ⬝ y) △) : False := by
  obtain ⟨p', y', _, _, heq⟩ := h.fork_inv
  exact Term.noConfusion heq

theorem Eval.fork_not_stem {p y x : Term} (h : Eval (△ ⬝ p ⬝ y) (△ ⬝ x)) :
    False := by
  obtain ⟨p', y', _, _, heq⟩ := h.fork_inv
  injection heq with h₁ _
  exact Term.noConfusion h₁

theorem Eval.stem_elim {x x₀ : Term} (h : Eval (△ ⬝ x) (△ ⬝ x₀)) : Eval x x₀ := by
  obtain ⟨x', hx, heq⟩ := h.stem_inv
  injection heq with _ h₂
  subst h₂
  exact hx

theorem Eval.fork_elim {p y p₀ y₀ : Term} (h : Eval (△ ⬝ p ⬝ y) (△ ⬝ p₀ ⬝ y₀)) :
    Eval p p₀ ∧ Eval y y₀ := by
  obtain ⟨p', y', hp, hy, heq⟩ := h.fork_inv
  injection heq with h₁ h₂
  injection h₁ with _ h₃
  subst h₃; subst h₂
  exact ⟨hp, hy⟩

/-! ## Preservation of eager evaluation under arbitrary steps -/

/-- If eager evaluation of `s` terminates with `v`, then after *any* single
reduction step (any position, any rule) it still terminates, with the same
value.  Orthogonality of the rules makes this go through by plain
induction/inversion. -/
theorem Eval.step_preserved {s s₁ v : Term} (h : Step s s₁) (he : Eval s v) :
    Eval s₁ v := by
  induction h generalizing v with
  | appL _ ih =>
    obtain ⟨s', t', hs, ht, ha⟩ := he.app_inv
    exact .app (ih hs) ht ha
  | appR _ ih =>
    obtain ⟨s', t', hs, ht, ha⟩ := he.app_inv
    exact .app hs (ih ht) ha
  | root hr =>
    cases hr with
    | k =>
      obtain ⟨c, z', hc, hz, ha⟩ := he.app_inv
      obtain ⟨p', y', hp, hy, rfl⟩ := hc.fork_inv
      cases hp.leaf_inv
      cases ha.k_inv
      exact hy
    | s =>
      obtain ⟨c, z', hc, hz, ha⟩ := he.app_inv
      obtain ⟨p', y', hp, hy, rfl⟩ := hc.fork_inv
      obtain ⟨x', hx, rfl⟩ := hp.stem_inv
      obtain ⟨xz, yz, h₁, h₂, h₃⟩ := ha.s_inv
      exact .app (.app hx hz h₁) (.app hy hz h₂) h₃
    | fLeaf =>
      obtain ⟨c, z', hc, hz, ha⟩ := he.app_inv
      obtain ⟨p', y', hp, hy, rfl⟩ := hc.fork_inv
      obtain ⟨w', x', hw, hx, rfl⟩ := hp.fork_inv
      cases hz.leaf_inv
      cases ha.fLeaf_inv
      exact hw
    | fStem =>
      obtain ⟨c, z', hc, hz, ha⟩ := he.app_inv
      obtain ⟨p', y', hp, hy, rfl⟩ := hc.fork_inv
      obtain ⟨w', x', hw, hx, rfl⟩ := hp.fork_inv
      obtain ⟨u', hu, rfl⟩ := hz.stem_inv
      exact .app hx hu ha.fStem_inv
    | fFork =>
      obtain ⟨c, z', hc, hz, ha⟩ := he.app_inv
      obtain ⟨p', y', hp, hy, rfl⟩ := hc.fork_inv
      obtain ⟨w', x', hw, hx, rfl⟩ := hp.fork_inv
      obtain ⟨u', v', hu, hv, rfl⟩ := hz.fork_inv
      obtain ⟨yu, hyu, hres⟩ := ha.fFork_inv
      exact .app (.app hy hu hyu) hv hres

/-! ## Strong normalization of applications -/

/-- Any step out of an application is a step in the left part, a step in the
right part, or a root contraction. -/
theorem step_app_cases {s t u : Term} (h : Step (s ⬝ t) u) :
    (∃ s₁, Step s s₁ ∧ u = s₁ ⬝ t) ∨ (∃ t₁, Step t t₁ ∧ u = s ⬝ t₁) ∨
      Root (s ⬝ t) u := by
  cases h with
  | root hr => exact .inr (.inr hr)
  | appL h => exact .inl ⟨_, h, rfl⟩
  | appR h => exact .inr (.inl ⟨_, h, rfl⟩)

/-- Closure lemma: `s ⬝ t` is strongly normalizing provided `s` and `t` are
and every root contraction of every reduct `s' ⬝ t'` yields a strongly
normalizing term.  The reducts `s'`, `t'` keep the eager values `a`, `b` of
`s`, `t` by `Eval.step_preserved`, which is how the root-contraction
hypothesis stays applicable.  Double well-founded induction on `SN s`, `SN t`. -/
theorem sn_app_closure {a b : Term}
    (H : ∀ s t u : Term, Eval s a → Eval t b → Root (s ⬝ t) u →
      SN s → SN t → SN u) :
    ∀ s : Term, SN s → ∀ t : Term, SN t → Eval s a → Eval t b → SN (s ⬝ t) := by
  intro s hsns
  induction hsns with
  | intro s hs ihs =>
    intro t hsnt
    induction hsnt with
    | intro t ht iht =>
      intro hes het
      refine Acc.intro _ fun u hu => ?_
      rcases step_app_cases hu with ⟨s₁, h₁, rfl⟩ | ⟨t₁, h₁, rfl⟩ | hroot
      · exact ihs s₁ h₁ t (Acc.intro t ht) (hes.step_preserved h₁) het
      · exact iht t₁ h₁ hes (het.step_preserved h₁)
      · exact H s t u hes het hroot (Acc.intro s hs) (Acc.intro t ht)

/-- The heart of the proof, by structural induction on the `Apply`
derivation: if applying the eager values of `s` and `t` to each other
terminates, and `s` and `t` are strongly normalizing, then so is `s ⬝ t`. -/
theorem apply_sn {a b v : Term} (hap : Apply a b v) :
    ∀ s t : Term, SN s → SN t → Eval s a → Eval t b → SN (s ⬝ t) := by
  induction hap with
  | @underLeaf b =>
    -- `a = △`: no reduct of `s` can expose a redex pattern at the root.
    intro s t hsns hsnt hes het
    refine sn_app_closure (fun s' t' u hes' het' hroot _ _ => ?_) s hsns t hsnt hes het
    cases hroot with
    | k => exact hes'.fork_not_leaf.elim
    | s => exact hes'.fork_not_leaf.elim
    | fLeaf => exact hes'.fork_not_leaf.elim
    | fStem => exact hes'.fork_not_leaf.elim
    | fFork => exact hes'.fork_not_leaf.elim
  | @underStem x b =>
    -- `a = △ ⬝ x`: still no root redex possible.
    intro s t hsns hsnt hes het
    refine sn_app_closure (fun s' t' u hes' het' hroot _ _ => ?_) s hsns t hsnt hes het
    cases hroot with
    | k => exact hes'.fork_not_stem.elim
    | s => exact hes'.fork_not_stem.elim
    | fLeaf => exact hes'.fork_not_stem.elim
    | fStem => exact hes'.fork_not_stem.elim
    | fFork => exact hes'.fork_not_stem.elim
  | @k y z =>
    -- Rule (1): the contractum is a subterm of `s'`.
    intro s t hsns hsnt hes het
    refine sn_app_closure (fun s' t' u hes' het' hroot hsns' hsnt' => ?_) s hsns t hsnt hes het
    cases hroot with
    | k => exact sn_app_right hsns'
    | s => exact (hes'.fork_elim).1.stem_not_leaf.elim
    | fLeaf => exact (hes'.fork_elim).1.fork_not_leaf.elim
    | fStem => exact (hes'.fork_elim).1.fork_not_leaf.elim
    | fFork => exact (hes'.fork_elim).1.fork_not_leaf.elim
  | @s x y z xz yz v hxz hyz hv ih₁ ih₂ ih₃ =>
    -- Rule (2): the contractum `x₀ ⬝ t' ⬝ (y₀ ⬝ t')` is handled by the
    -- induction hypotheses for the three sub-applications.
    intro s t hsns hsnt hes het
    refine sn_app_closure (fun s' t' u hes' het' hroot hsns' hsnt' => ?_) s hsns t hsnt hes het
    cases hroot with
    | k => exact (hes'.fork_elim).1.leaf_not_stem.elim
    | @s x₀ y₀ _ =>
      obtain ⟨hp, hy₀⟩ := hes'.fork_elim
      have hx₀ : Eval x₀ x := hp.stem_elim
      have hsnx₀ : SN x₀ := sn_app_right (sn_app_right (sn_app_left hsns'))
      have hsny₀ : SN y₀ := sn_app_right hsns'
      have hxt : SN (x₀ ⬝ t') := ih₁ x₀ t' hsnx₀ hsnt' hx₀ het'
      have hyt : SN (y₀ ⬝ t') := ih₂ y₀ t' hsny₀ hsnt' hy₀ het'
      exact ih₃ (x₀ ⬝ t') (y₀ ⬝ t') hxt hyt
        (.app hx₀ het' hxz) (.app hy₀ het' hyz)
    | fLeaf => exact (hes'.fork_elim).1.fork_not_stem.elim
    | fStem => exact (hes'.fork_elim).1.fork_not_stem.elim
    | fFork => exact (hes'.fork_elim).1.fork_not_stem.elim
  | @fLeaf w x y =>
    -- Rule (3a): the contractum is a subterm of `s'`.
    intro s t hsns hsnt hes het
    refine sn_app_closure (fun s' t' u hes' het' hroot hsns' hsnt' => ?_) s hsns t hsnt hes het
    cases hroot with
    | k => exact (hes'.fork_elim).1.leaf_not_fork.elim
    | s => exact (hes'.fork_elim).1.stem_not_fork.elim
    | fLeaf => exact sn_app_right (sn_app_left (sn_app_right (sn_app_left hsns')))
    | fStem => exact het'.stem_not_leaf.elim
    | fFork => exact het'.fork_not_leaf.elim
  | @fStem w x y u v hxu ih₁ =>
    -- Rule (3b): the contractum `x₀ ⬝ u₀` is handled by the induction
    -- hypothesis for the sub-application.
    intro s t hsns hsnt hes het
    refine sn_app_closure (fun s' t' u' hes' het' hroot hsns' hsnt' => ?_) s hsns t hsnt hes het
    cases hroot with
    | k => exact (hes'.fork_elim).1.leaf_not_fork.elim
    | s => exact (hes'.fork_elim).1.stem_not_fork.elim
    | fLeaf => exact het'.leaf_not_stem.elim
    | @fStem _ x₀ _ u₀ =>
      obtain ⟨hp, _⟩ := hes'.fork_elim
      have hx₀ : Eval x₀ x := (hp.fork_elim).2
      have hu₀ : Eval u₀ u := het'.stem_elim
      have hsnx₀ : SN x₀ := sn_app_right (sn_app_right (sn_app_left hsns'))
      have hsnu₀ : SN u₀ := sn_app_right hsnt'
      exact ih₁ x₀ u₀ hsnx₀ hsnu₀ hx₀ hu₀
    | fFork => exact het'.fork_not_stem.elim
  | @fFork w x y u v yu r hyu hr ih₁ ih₂ =>
    -- Rule (3c): the contractum `y₀ ⬝ u₀ ⬝ v₀` is handled by the induction
    -- hypotheses for the two sub-applications.
    intro s t hsns hsnt hes het
    refine sn_app_closure (fun s' t' u' hes' het' hroot hsns' hsnt' => ?_) s hsns t hsnt hes het
    cases hroot with
    | k => exact (hes'.fork_elim).1.leaf_not_fork.elim
    | s => exact (hes'.fork_elim).1.stem_not_fork.elim
    | fLeaf => exact het'.leaf_not_fork.elim
    | fStem => exact het'.stem_not_fork.elim
    | @fFork _ _ y₀ u₀ v₀ =>
      obtain ⟨_, hy₀⟩ := hes'.fork_elim
      obtain ⟨hu₀, hv₀⟩ := het'.fork_elim
      have hsny₀ : SN y₀ := sn_app_right hsns'
      have hsnu₀ : SN u₀ := sn_app_right (sn_app_left hsnt')
      have hsnv₀ : SN v₀ := sn_app_right hsnt'
      have h₁ : SN (y₀ ⬝ u₀) := ih₁ y₀ u₀ hsny₀ hsnu₀ hy₀ hu₀
      exact ih₂ (y₀ ⬝ u₀) v₀ h₁ hsnv₀ (.app hy₀ hu₀ hyu) hv₀

/-! ## Main results -/

/-- **Main theorem.**  If eager evaluation of `t` terminates then `t` is
strongly normalizing: every reduction sequence out of `t`, under any
strategy, is finite. -/
theorem Eval.sn {t v : Term} (h : Eval t v) : SN t := by
  induction h with
  | leaf => exact sn_leaf
  | app hs ht ha ihs iht => exact apply_sn ha _ _ ihs iht hs ht

/-- Termination of eager evaluation, phrased without naming the result. -/
theorem sn_of_eager_terminating {t : Term} (h : ∃ v, Eval t v) : SN t :=
  h.choose_spec.sn

/-- Restatement in terms of the executable evaluator: if `evalF` returns a
result for some amount of fuel, the input is strongly normalizing. -/
theorem sn_of_evalF {n : Nat} {t v : Term} (h : evalF n t = some v) : SN t :=
  (evalF_sound h).sn

/-- Summary: when eager evaluation terminates on `t` with value `v`, then
`t` is strongly normalizing, and `v` is a genuine normal form of `t`:
reachable by reduction (`Eval.steps`) and irreducible
(`Eval.isValue` + `IsValue.no_step`). -/
theorem Eval.sn_steps_value {t v : Term} (h : Eval t v) :
    SN t ∧ Steps t v ∧ IsValue v :=
  ⟨h.sn, h.steps, h.isValue⟩

end Term

end TreeCalculus
