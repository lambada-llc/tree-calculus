import TreeCalculus.Check
import TreeCalculus.Runtime

/-!
# Rules: bigger steps over patterns ("first-order jets")

A *rule* is a pair of symbolic states: every instance of its left side runs, by `step`, to the
same instance of its right side (`Rule.Holds`). The right side — the *residual* — is any state: a
value (`dispatch r []`) or a pending computation (`reduce`, frames).

* `checkRule ρ ℓ`: the symbolic run (`sstep`) from `ρ.lhs` arrives at `ρ.rhs` after exactly `ℓ`
  transitions, without a split. Then every instance takes exactly `ℓ` `step`s (`checkRule_steps`),
  so it `Holds` (`checkRule_sound`), and so does every refinement of it (`checkRule_inst`).
* `RuleJet rules`: what a runtime may do with rules — from an instance of a left side, with any
  frames under it, jump to the same instance of the right side. `RuleJet.reaches` makes it a `J`
  of `runtime_sound`: nothing in `RStep` changes.
* Shape-dependent rules (a decision tree of case splits) are lists of rules whose left sides are
  refined (`RuleTree.rules`); the tree is an index for matching, not part of soundness.
* The memo's entries are the ground rules (`Memo.sound_iff`).
-/

namespace TreeCalculus

open Term
open SValue

deriving instance DecidableEq for SValue
deriving instance DecidableEq for Frame
deriving instance DecidableEq for State

/-! ## Runs of a given length -/

/-- `s` after exactly `n` steps, if the loop has not returned before. -/
def steps : Nat → State Value → Option (State Value)
  | 0, s => some s
  | n + 1, s => (step s).bind (steps n)

/-- `s` after exactly `n` symbolic steps, if none of them splits or halts. -/
def ssteps : Nat → State SValue → Option (State SValue)
  | 0, s => some s
  | n + 1, s => match sstep s with
    | .next s' => ssteps n s'
    | _ => none

theorem steps_reaches (h : steps n s = some t) : Reaches s t := by
  induction n generalizing s with
  | zero => cases h; exact .refl _
  | succ n ih =>
    simp only [steps, Option.bind_eq_some_iff] at h
    obtain ⟨s', hs, h⟩ := h
    exact .head hs (ih h)

theorem steps_app (h : steps n s = some t) (k) : steps n (s.app k) = some (t.app k) := by
  induction n generalizing s with
  | zero => cases h; rfl
  | succ n ih =>
    simp only [steps, Option.bind_eq_some_iff] at h
    obtain ⟨s', hs, h⟩ := h
    simp [steps, step_app hs, ih h]

/-- A symbolic run of `n` steps is a run of `n` steps of every instance (`sstep_next`, iterated). -/
theorem ssteps_subst (h : ssteps n s = some t) (σ : Nat → Value) :
    steps n (s.map (subst σ)) = some (t.map (subst σ)) := by
  induction n generalizing s with
  | zero => cases h; rfl
  | succ n ih =>
    simp only [ssteps] at h
    split at h
    · simp [steps, sstep_next ‹_› σ, ih h]
    · cases h

/-! ## Refinement: a symbolic run is a run of every refinement -/

namespace SValue

theorem view_inst_leaf {a : SValue} (h : a.view = .leaf) : (a.inst i p).view = .leaf := by
  match a, h with | lit .leaf, _ => rfl

theorem view_inst_stem {a : SValue} (h : a.view = .stem u) :
    (a.inst i p).view = .stem (u.inst i p) := by
  match a, h with
  | lit (.stem _), rfl => rfl
  | stem _, rfl => rfl

theorem view_inst_fork {a : SValue} (h : a.view = .fork u v) :
    (a.inst i p).view = .fork (u.inst i p) (v.inst i p) := by
  match a, h with
  | lit (.fork _ _), rfl => rfl
  | fork _ _, rfl => rfl

end SValue

/-- `sstep_next`'s counterpart for refinement: a step `sstep` takes on `s` it takes on `s` with
`var i` replaced by any pattern `p`. -/
theorem sstep_inst {s : State SValue} (h : sstep s = .next s') (i : Nat) (p : SValue) :
    sstep (s.map (·.inst i p)) = .next (s'.map (·.inst i p)) := by
  match s with
  | .reduce a b k =>
    simp only [sstep] at h
    split at h
    · cases h
    · cases h; simp [sstep, State.map, inst, view_inst_leaf ‹_›]
    · cases h; simp [sstep, State.map, inst, view_inst_stem ‹_›]
    · rename_i u y ha; split at h
      · cases h
      · cases h; simp [sstep, State.map, view_inst_fork ha, view_inst_leaf ‹_›]
      · cases h; simp [sstep, State.map, Frame.map, view_inst_fork ha, view_inst_stem ‹_›]
      · rename_i w x hu; split at h
        · cases h
        · cases h
          simp [sstep, State.map, view_inst_fork ha, view_inst_fork hu, view_inst_leaf ‹_›]
        · cases h
          simp [sstep, State.map, view_inst_fork ha, view_inst_fork hu, view_inst_stem ‹_›]
        · cases h
          simp [sstep, State.map, Frame.map, view_inst_fork ha, view_inst_fork hu,
            view_inst_fork ‹_›]
  | .dispatch _ [] => cases h
  | .dispatch _ (.applyTo _ :: _) => cases h; rfl
  | .dispatch _ (.computeAndApply _ _ :: _) => cases h; rfl

theorem ssteps_inst (h : ssteps n s = some t) (i : Nat) (p : SValue) :
    ssteps n (s.map (·.inst i p)) = some (t.map (·.inst i p)) := by
  induction n generalizing s with
  | zero => cases h; rfl
  | succ n ih =>
    simp only [ssteps] at h
    split at h
    · simp [ssteps, sstep_inst ‹_› i p, ih h]
    · cases h

/-! ## Rules -/

/-- `lhs` runs to `rhs`, instance by instance. -/
structure Rule where
  lhs : State SValue
  rhs : State SValue

namespace Rule

/-- Every instance of the left side runs to the same instance of the right side. -/
def Holds (ρ : Rule) : Prop := ∀ σ, Reaches (ρ.lhs.map (subst σ)) (ρ.rhs.map (subst σ))

/-- Both sides with `var i` replaced by `p`. -/
def inst (ρ : Rule) (i : Nat) (p : SValue) : Rule :=
  ⟨ρ.lhs.map (·.inst i p), ρ.rhs.map (·.inst i p)⟩

end Rule

/-- The symbolic run from `ρ.lhs` arrives at `ρ.rhs` after exactly `ℓ` transitions, none of them a
split. Decided by the kernel (`decide +kernel`, as `Cpp/Jets.lean` does for `skipLine`). -/
def checkRule (ρ : Rule) (ℓ : Nat) : Bool := ssteps ℓ ρ.lhs = some ρ.rhs

/-- A checked rule is exactly `ℓ` machine steps on every instance. -/
theorem checkRule_steps (h : checkRule ρ ℓ = true) (σ : Nat → Value) :
    steps ℓ (ρ.lhs.map (subst σ)) = some (ρ.rhs.map (subst σ)) :=
  ssteps_subst (of_decide_eq_true h) σ

theorem checkRule_sound (h : checkRule ρ ℓ = true) : ρ.Holds :=
  fun σ => steps_reaches (checkRule_steps h σ)

/-- A rule checked on a general pattern is checked on every special one, with the same length. -/
theorem checkRule_inst (h : checkRule ρ ℓ = true) (i : Nat) (p : SValue) :
    checkRule (ρ.inst i p) ℓ = true :=
  decide_eq_true (ssteps_inst (of_decide_eq_true h) i p)

/-- A list of rules with their lengths, every one checked. -/
def checkRules (rs : List (Rule × Nat)) : Bool := rs.all fun r => checkRule r.1 r.2

theorem checkRules_sound (h : checkRules rs = true) : ∀ ρ ∈ rs.map (·.1), ρ.Holds := by
  simp only [List.mem_map]
  rintro _ ⟨r, hr, rfl⟩
  exact checkRule_sound (List.all_eq_true.1 h r hr)

/-- Rules with their lengths, checked: what a generated file collects, one theorem per group, so
that no single proof has to look at all of them. -/
structure Checked where
  rules : List (Rule × Nat)
  ok : checkRules rules = true

/-- One checked rule. -/
def Checked.one (h : checkRule ρ ℓ = true) : Checked := ⟨[(ρ, ℓ)], by simp [checkRules, h]⟩

/-- The rules of checked groups are checked. -/
theorem Checked.ok_all (cs : List Checked) : checkRules (cs.flatMap (·.rules)) = true := by
  induction cs with
  | nil => rfl
  | cons c _ ih =>
    simp only [List.flatMap_cons, checkRules, List.all_append, Bool.and_eq_true]; exact ⟨c.ok, ih⟩

/-! ## Rules are jets -/

/-- What a runtime may do with rules: jump from an instance of a left side, under any stack `k`,
to the same instance of its right side. -/
inductive RuleJet (rules : List Rule) : State Value → State Value → Prop
  | fire {ρ : Rule} {σ : Nat → Value} {k : List (Frame Value)} : ρ ∈ rules →
      RuleJet rules ((ρ.lhs.map (subst σ)).app k) ((ρ.rhs.map (subst σ)).app k)

theorem RuleJet.reaches {rules : List Rule} (h : ∀ ρ ∈ rules, ρ.Holds) :
    RuleJet rules s t → Reaches s t
  | .fire hρ => (h _ hρ _).app _

/-- `runtime_sound`, unchanged, for a runtime that fires rules that hold. -/
theorem rules_runtime_sound {rules : List Rule} (h : ∀ ρ ∈ rules, ρ.Holds) (hm : m.Sound)
    (hr : Star (RStep (RuleJet rules)) ⟨.reduce a b [], [], m⟩ ⟨.dispatch r [], [], m'⟩) :
    Apply a.toTerm b.toTerm r.toTerm ∧ m'.Sound :=
  runtime_sound (fun _ _ => RuleJet.reaches h) hm hr

/-! ## Closure: instantiation, chaining, frames -/

namespace Rule.Holds

/-- A rule holds for every refinement of its variables. -/
theorem inst {ρ : Rule} (h : ρ.Holds) (i : Nat) (p : SValue) : (ρ.inst i p).Holds := fun σ => by
  simpa only [Rule.inst, State.subst_inst] using h (upd σ i (p.subst σ))

/-- Rules chain where one's residual is the next one's left side. -/
theorem trans (h₁ : (⟨s, t⟩ : Rule).Holds) (h₂ : (⟨t, u⟩ : Rule).Holds) :
    (⟨s, u⟩ : Rule).Holds := fun σ => (h₁ σ).trans (h₂ σ)

theorem _root_.TreeCalculus.State.map_app (s : State α) (k : List (Frame α)) (f : α → β) :
    (s.app k).map f = (s.map f).app (k.map (·.map f)) := by
  cases s <;> simp [State.app, State.map, List.map_append]

/-- A rule may be stated with symbolic frames underneath: they ride along. -/
theorem app {ρ : Rule} (h : ρ.Holds) (k : List (Frame SValue)) :
    (⟨ρ.lhs.app k, ρ.rhs.app k⟩ : Rule).Holds := fun σ => by
  simpa only [State.map_app] using (h σ).app (k.map (·.map (subst σ)))

end Rule.Holds

/-! ## Order 0: the memo's entries are ground rules -/

def memoRule (a b r : Value) : Rule := ⟨.reduce (.lit a) (.lit b) [], .dispatch (.lit r) []⟩

theorem memoRule_holds_iff : (memoRule a b r).Holds ↔ Apply a.toTerm b.toTerm r.toTerm :=
  ⟨fun h => run_iff_apply.1 (h fun _ => .leaf), fun h _ => run_iff_apply.2 h⟩

/-- `Memo.Sound` (`Runtime.lean`) says exactly: every entry is a ground rule that holds. -/
theorem Memo.sound_iff (m : Memo) : m.Sound ↔ ∀ a b r, m a b r → (memoRule a b r).Holds :=
  ⟨fun h a b r e => memoRule_holds_iff.2 (h a b r e),
    fun h a b r e => memoRule_holds_iff.1 (h a b r e)⟩

/-! ## Order 2: decision trees of rules -/

/-- A case-split tree over a left side: at `split i j`, `var i` is `△`, `△ (var j)`, or
`△ (var j) (var (j + 1))` (`shapes j`); a leaf names its branch's residual and length. -/
inductive RuleTree where
  | leaf (rhs : State SValue) (ℓ : Nat)
  | split (i j : Nat) (l s f : RuleTree)

/-- The rules a tree stands for: one per leaf, its left side refined along the path to it. -/
def RuleTree.rules : State SValue → RuleTree → List (Rule × Nat)
  | lhs, .leaf rhs ℓ => [(⟨lhs, rhs⟩, ℓ)]
  | lhs, .split i j l s f =>
    l.rules (lhs.map (·.inst i (lit .leaf))) ++ s.rules (lhs.map (·.inst i (stem (var j)))) ++
      f.rules (lhs.map (·.inst i (fork (var j) (var (j + 1)))))

-- Exhaustiveness of a tree (every instance of `lhs` is an instance of some leaf: `shapes_cover`
-- with `j` fresh) matters for how often a jet fires, never for soundness: `checkRules_sound`.

/-! ## Length: a runtime firing rules still pays for the machine's steps -/

/-- `n` transitions of `r`. -/
inductive StarN (r : α → α → Prop) : Nat → α → α → Prop
  | refl (a : α) : StarN r 0 a a
  | head : r a b → StarN r n b c → StarN r (n + 1) a c

theorem steps_add (h₁ : steps m s = some t) (h₂ : steps n t = some u) :
    steps (m + n) s = some u := by
  induction m generalizing s with
  | zero => cases h₁; simpa using h₂
  | succ m ih =>
    simp only [steps, Option.bind_eq_some_iff] at h₁
    obtain ⟨s', hs, h₁⟩ := h₁
    rw [Nat.add_right_comm]; simp [steps, hs, ih h₁]

/-- `step` is deterministic and stops at `dispatch r []`, so the number of steps to it is fixed. -/
theorem steps_halt_unique (h₁ : steps m s = some (.dispatch r []))
    (h₂ : steps n s = some (.dispatch r' [])) : m = n ∧ r = r' := by
  induction m generalizing s n with
  | zero =>
    cases h₁; cases n with
    | zero => cases h₂; exact ⟨rfl, rfl⟩
    | succ n => simp [steps, step] at h₂
  | succ m ih =>
    simp only [steps, Option.bind_eq_some_iff] at h₁
    obtain ⟨s', hs, h₁⟩ := h₁
    cases n with
    | zero => cases h₂; simp [step] at hs
    | succ n =>
      simp only [steps, hs, Option.bind_some] at h₂
      obtain ⟨rfl, rfl⟩ := ih h₁ h₂; exact ⟨rfl, rfl⟩

/-- A run of `M` transitions — machine steps or jets that each stand for at most `L ≥ 1` of them —
is a machine run of at most `M * L` steps. -/
theorem StarN.steps {J : State Value → State Value → Prop} (hL : 1 ≤ L)
    (hj : ∀ s t, J s t → ∃ ℓ ≤ L, TreeCalculus.steps ℓ s = some t)
    (h : StarN (fun s t => step s = some t ∨ J s t) M s t) :
    ∃ N ≤ M * L, TreeCalculus.steps N s = some t := by
  induction h with
  | refl => exact ⟨0, Nat.zero_le _, rfl⟩
  | head hst _ ih =>
    obtain ⟨N, hN, h⟩ := ih
    rcases hst with hs | hs
    · exact ⟨1 + N, by rw [Nat.succ_mul]; omega, steps_add (by simp [TreeCalculus.steps, hs]) h⟩
    · obtain ⟨ℓ, hℓ, hs⟩ := hj _ _ hs
      exact ⟨ℓ + N, by rw [Nat.succ_mul]; omega, steps_add hs h⟩

/-- Rules of length at most `L` save at most a factor `L`: if the machine takes `N` steps from
`reduce a b []` to its result, a run firing them takes at least `N / L` transitions. -/
theorem rules_length_bound {rules : List (Rule × Nat)} (hc : checkRules rules = true)
    (hL : 1 ≤ L) (hℓ : ∀ r ∈ rules, r.2 ≤ L)
    (hN : steps N (.reduce a b []) = some (.dispatch v []))
    (h : StarN (fun s t => step s = some t ∨ RuleJet (rules.map (·.1)) s t) M
      (.reduce a b []) (.dispatch r [])) :
    N ≤ M * L := by
  obtain ⟨N', hN', h⟩ := h.steps hL fun s t => by
    rintro ⟨hρ⟩
    obtain ⟨⟨ρ, ℓ⟩, hr, rfl⟩ := List.mem_map.1 hρ
    exact ⟨ℓ, hℓ _ hr, steps_app (checkRule_steps (List.all_eq_true.1 hc _ hr) _) _⟩
  obtain ⟨rfl, -⟩ := steps_halt_unique hN h; exact hN'

end TreeCalculus
