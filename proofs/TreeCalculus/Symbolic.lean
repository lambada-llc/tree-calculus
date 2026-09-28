import TreeCalculus.Machine

/-!
# Symbolic execution

The machine of `Machine.lean`, run on trees with variables. `sstep` takes the step `step` would,
except where the rule to pick depends on the shape of a variable: there it reports which one
(`stuck i`) instead of guessing. The substitution lemma `sstep_next` says every step it does take
is the step `step` takes on every instance.

Concrete subtrees are embedded as `lit t` and never walked: `view` exposes one level at a time,
and `inst`/`maxVar` stop at a `lit`. A concrete tree written as one Lean definition per DAG node
(`dag2lean.mjs`) therefore costs the kernel one delta-unfolding per node the run actually visits,
not its tree expansion — and comparing two of them (`decide (u = t)` in `fits`) costs one pair of
nodes per *distinct* pair, because the kernel caches every `whnf` it computes, keyed by term: the
recursive comparisons of a shared node pair are the same term (`Tests.lean` pins that down on a
tree with 2^60 leaves).
-/

namespace TreeCalculus

inductive STree where
  | lit (t : Tree)
  | stem (u : STree)
  | fork (u v : STree)
  | var (i : Nat)

namespace STree

def subst (σ : Nat → Tree) : STree → Tree
  | lit t => t
  | stem u => .stem (u.subst σ)
  | fork u v => .fork (u.subst σ) (v.subst σ)
  | var i => σ i

/-- `x` with `var i` replaced by `p`. -/
def inst : (x : STree) → (i : Nat) → (p : STree) → STree
  | lit t, _, _ => lit t
  | stem u, i, p => stem (u.inst i p)
  | fork u v, i, p => fork (u.inst i p) (v.inst i p)
  | var j, i, p => if j = i then p else var j

def maxVar : STree → Nat
  | lit _ => 0
  | stem u => u.maxVar
  | fork u v => max u.maxVar v.maxVar
  | var i => i

/-- Whether `x` has the shape of `t`, a variable fitting anything if `any` and nothing if not:
`fits false x t` says `x` is `t` whatever its variables are (`fits_false`), and `fits true x t`
is necessary for `t` to be an instance of `x` at all (`fits_true`). -/
def fits (any : Bool) : STree → Tree → Bool
  | lit u, t => decide (u = t)
  | stem u, .stem t => u.fits any t
  | fork u v, .fork t w => u.fits any t && v.fits any w
  | var _, _ => any
  | _, _ => false

/-- One level of shape: what triage reads off a node, or the variable that decides it. -/
inductive View where
  | leaf
  | stem (u : STree)
  | fork (u v : STree)
  | var (i : Nat)

def view : STree → View
  | lit .leaf => .leaf
  | lit (.stem u) => .stem (lit u)
  | lit (.fork u v) => .fork (lit u) (lit v)
  | stem u => .stem u
  | fork u v => .fork u v
  | var i => .var i

end STree

/-- What a symbolic step comes to. -/
inductive Out where
  | next (s : State STree)
  /-- The rule depends on the shape of `var i`. -/
  | stuck (i : Nat)
  /-- `return result`. -/
  | halt

/-- `step`, triaging through `view`. -/
def sstep : State STree → Out
  | .reduce a b k => match a.view with
    | .var i => .stuck i
    | .leaf => .next (.dispatch (.stem b) k)
    | .stem u => .next (.dispatch (.fork u b) k)
    | .fork u y => match u.view with
      | .var i => .stuck i
      | .leaf => .next (.dispatch y k)
      | .stem u' => .next (.reduce y b (.computeAndApply u' b :: k))
      | .fork w x => match b.view with
        | .var i => .stuck i
        | .leaf => .next (.dispatch w k)
        | .stem d => .next (.reduce x d k)
        | .fork d e => .next (.reduce y d (.applyTo e :: k))
  | .dispatch _ [] => .halt
  | .dispatch r (.applyTo e :: k) => .next (.reduce r e k)
  | .dispatch r (.computeAndApply f e :: k) => .next (.reduce f e (.applyTo r :: k))

/-! ## Substitution -/

namespace STree

/-- `σ` with `i` sent to `v`. -/
def upd (σ : Nat → Tree) (i : Nat) (v : Tree) : Nat → Tree := fun j => if j = i then v else σ j

theorem subst_inst (x : STree) : (x.inst i p).subst σ = x.subst (upd σ i (p.subst σ)) := by
  induction x with
  | var j => by_cases h : j = i <;> simp [inst, subst, upd, h]
  | _ => simp_all [inst, subst]

theorem subst_congr (x : STree) (h : ∀ j ≤ x.maxVar, σ j = σ' j) : x.subst σ = x.subst σ' := by
  induction x with
  | lit => rfl
  | stem _ ih => exact congrArg _ (ih h)
  | fork _ _ ih₁ ih₂ =>
    simp only [maxVar, subst] at *
    rw [ih₁ fun j hj => h j (by omega), ih₂ fun j hj => h j (by omega)]
  | var i => exact h i (Nat.le_refl _)

theorem fits_false {x : STree} (h : x.fits false t = true) : x.subst σ = t := by
  induction x generalizing t with
  | lit => simpa [fits, subst] using h
  | stem _ ih => cases t <;> simp only [fits, reduceCtorEq] at h; simp [subst, ih h]
  | fork _ _ ih₁ ih₂ =>
    cases t <;> simp only [fits, Bool.and_eq_true, reduceCtorEq] at h
    simp [subst, ih₁ h.1, ih₂ h.2]
  | var => simp [fits] at h

theorem fits_true {x : STree} (h : x.subst σ = t) : x.fits true t = true := by
  induction x generalizing t with
  | lit => subst h; simp [fits, subst]
  | stem _ ih => subst h; exact ih rfl
  | fork _ _ ih₁ ih₂ => subst h; show (_ && _) = true; rw [ih₁ rfl, ih₂ rfl]; rfl
  | var => rfl

theorem view_leaf {a : STree} (h : a.view = .leaf) : a.subst σ = .leaf := by
  match a, h with | lit .leaf, _ => rfl

theorem view_stem {a : STree} (h : a.view = .stem u) : a.subst σ = .stem (u.subst σ) := by
  match a, h with
  | lit (.stem _), rfl => rfl
  | stem _, rfl => rfl

theorem view_fork {a : STree} (h : a.view = .fork u v) :
    a.subst σ = .fork (u.subst σ) (v.subst σ) := by
  match a, h with
  | lit (.fork _ _), rfl => rfl
  | fork _ _, rfl => rfl

end STree

open STree

/-- The substitution lemma: a step `sstep` takes is `step`'s on every instance. -/
theorem sstep_next {s : State STree} (h : sstep s = .next s') (σ : Nat → Tree) :
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
theorem State.subst_inst (s : State STree) :
    (s.map (·.inst i p)).map (subst σ) = s.map (subst (upd σ i (p.subst σ))) := by
  have hf : ∀ f : Frame STree,
      (f.map (·.inst i p)).map (subst σ) = f.map (subst (upd σ i (p.subst σ))) := by
    intro f; cases f <;> simp [Frame.map, STree.subst_inst]
  cases s <;> simp [State.map, STree.subst_inst, hf]

end TreeCalculus
