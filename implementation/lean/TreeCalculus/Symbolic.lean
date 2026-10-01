import TreeCalculus.Machine

/-!
# Symbolic execution

`sstep` is `step` on values with variables, returning `split i` where the rule depends on the
shape of `var i`. `sstep_next`: a step it takes is `step`'s on every instance. `lit t` embeds a
value without walking it: `view` exposes one level at a time.
-/

namespace TreeCalculus

/-- A value with variables (`var i`), and with values embedded whole (`lit t`). -/
inductive SValue where
  | lit (t : Value)
  | stem (u : SValue)
  | fork (u v : SValue)
  | var (i : Nat)

namespace SValue

def subst (σ : Nat → Value) : SValue → Value
  | lit t => t
  | stem u => .stem (u.subst σ)
  | fork u v => .fork (u.subst σ) (v.subst σ)
  | var i => σ i

/-- `x` with `var i` replaced by `p`. -/
def inst : (x : SValue) → (i : Nat) → (p : SValue) → SValue
  | lit t, _, _ => lit t
  | stem u, i, p => stem (u.inst i p)
  | fork u v, i, p => fork (u.inst i p) (v.inst i p)
  | var j, i, p => if j = i then p else var j

def maxVar : SValue → Nat
  | lit _ => 0
  | stem u => u.maxVar
  | fork u v => max u.maxVar v.maxVar
  | var i => i

/-- Whether `x` has the shape of `t`, a variable fitting anything if `any` and nothing if not:
`fits false x t` says `x` is `t` whatever its variables are (`fits_false`), and `fits true x t`
is necessary for `t` to be an instance of `x` at all (`fits_true`). -/
def fits (any : Bool) : SValue → Value → Bool
  | lit u, t => decide (u = t)
  | stem u, .stem t => u.fits any t
  | fork u v, .fork t w => u.fits any t && v.fits any w
  | var _, _ => any
  | _, _ => false

/-- One level of shape: what triage reads off a node, or the variable that decides it. -/
inductive View where
  | leaf
  | stem (u : SValue)
  | fork (u v : SValue)
  | var (i : Nat)

def view : SValue → View
  | lit .leaf => .leaf
  | lit (.stem u) => .stem (lit u)
  | lit (.fork u v) => .fork (lit u) (lit v)
  | stem u => .stem u
  | fork u v => .fork u v
  | var i => .var i

end SValue

/-- What a symbolic step comes to. -/
inductive Out where
  | next (s : State SValue)
  /-- The rule depends on the shape of `var i`. -/
  | split (i : Nat)
  /-- `step` takes none: the loop returns. -/
  | halt

/-- `step`, triaging through `view`. -/
def sstep : State SValue → Out
  | .reduce a b k => match a.view with
    | .var i => .split i
    | .leaf => .next (.dispatch (.stem b) k)
    | .stem u => .next (.dispatch (.fork u b) k)
    | .fork u y => match u.view with
      | .var i => .split i
      | .leaf => .next (.dispatch y k)
      | .stem u' => .next (.reduce y b (.computeAndApply u' b :: k))
      | .fork w x => match b.view with
        | .var i => .split i
        | .leaf => .next (.dispatch w k)
        | .stem d => .next (.reduce x d k)
        | .fork d e => .next (.reduce y d (.applyTo e :: k))
  | .dispatch _ [] => .halt
  | .dispatch r (.applyTo e :: k) => .next (.reduce r e k)
  | .dispatch r (.computeAndApply f e :: k) => .next (.reduce f e (.applyTo r :: k))

/-! ## Substitution -/

namespace SValue

/-- `σ` with `i` sent to `v`. -/
def upd (σ : Nat → Value) (i : Nat) (v : Value) : Nat → Value := fun j => if j = i then v else σ j

theorem subst_inst (x : SValue) : (x.inst i p).subst σ = x.subst (upd σ i (p.subst σ)) := by
  induction x with
  | var j => by_cases h : j = i <;> simp [inst, subst, upd, h]
  | _ => simp_all [inst, subst]

theorem subst_congr (x : SValue) (h : ∀ j ≤ x.maxVar, σ j = σ' j) : x.subst σ = x.subst σ' := by
  induction x with
  | lit => rfl
  | stem _ ih => exact congrArg _ (ih h)
  | fork _ _ ih₁ ih₂ =>
    simp only [maxVar, subst] at *
    rw [ih₁ fun j hj => h j (by omega), ih₂ fun j hj => h j (by omega)]
  | var i => exact h i (Nat.le_refl _)

theorem fits_false {x : SValue} (h : x.fits false t = true) : x.subst σ = t := by
  induction x generalizing t with
  | lit => simpa [fits, subst] using h
  | stem _ ih => cases t <;> simp only [fits, reduceCtorEq] at h; simp [subst, ih h]
  | fork _ _ ih₁ ih₂ =>
    cases t <;> simp only [fits, Bool.and_eq_true, reduceCtorEq] at h
    simp [subst, ih₁ h.1, ih₂ h.2]
  | var => simp [fits] at h

theorem fits_true {x : SValue} (h : x.subst σ = t) : x.fits true t = true := by
  induction x generalizing t with
  | lit => subst h; simp [fits, subst]
  | stem _ ih => subst h; exact ih rfl
  | fork _ _ ih₁ ih₂ => subst h; show (_ && _) = true; rw [ih₁ rfl, ih₂ rfl]; rfl
  | var => rfl

theorem view_leaf {a : SValue} (h : a.view = .leaf) : a.subst σ = .leaf := by
  match a, h with | lit .leaf, _ => rfl

theorem view_stem {a : SValue} (h : a.view = .stem u) : a.subst σ = .stem (u.subst σ) := by
  match a, h with
  | lit (.stem _), rfl => rfl
  | stem _, rfl => rfl

theorem view_fork {a : SValue} (h : a.view = .fork u v) :
    a.subst σ = .fork (u.subst σ) (v.subst σ) := by
  match a, h with
  | lit (.fork _ _), rfl => rfl
  | fork _ _, rfl => rfl

end SValue

open SValue

/-- The substitution lemma: a step `sstep` takes is `step`'s on every instance. -/
theorem sstep_next {s : State SValue} (h : sstep s = .next s') (σ : Nat → Value) :
    step (s.map (subst σ)) = some (s'.map (subst σ)) := by
  match s with
  | .reduce a b k =>
    simp only [sstep] at h
    split at h
    · cases h
    · cases h; simp [step, State.map, subst, view_leaf ‹_›]
    · cases h; simp [step, State.map, subst, view_stem ‹_›]
    · rename_i u y ha; split at h
      · cases h
      · cases h; simp [step, State.map, view_fork ha, view_leaf ‹_›]
      · cases h; simp [step, State.map, Frame.map, view_fork ha, view_stem ‹_›]
      · rename_i w x hu; split at h
        · cases h
        · cases h; simp [step, State.map, view_fork ha, view_fork hu, view_leaf ‹_›]
        · cases h; simp [step, State.map, view_fork ha, view_fork hu, view_stem ‹_›]
        · cases h; simp [step, State.map, Frame.map, view_fork ha, view_fork hu, view_fork ‹_›]
  | .dispatch _ [] => cases h
  | .dispatch _ (.applyTo _ :: _) => cases h; rfl
  | .dispatch _ (.computeAndApply _ _ :: _) => cases h; rfl

/-- `inst`, then `subst`, is `subst` alone at the updated valuation — on whole states. -/
theorem State.subst_inst (s : State SValue) :
    (s.map (·.inst i p)).map (subst σ) = s.map (subst (upd σ i (p.subst σ))) := by
  have hf : ∀ f : Frame SValue,
      (f.map (·.inst i p)).map (subst σ) = f.map (subst (upd σ i (p.subst σ))) := by
    intro f; cases f <;> simp [Frame.map, SValue.subst_inst]
  cases s <;> simp [State.map, SValue.subst_inst, hf]

end TreeCalculus
