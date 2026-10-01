import TreeCalculus.Value

/-!
# The eager machine

`Apply` as a loop: `reduce` and `dispatch` over a stack of `Frame`s. `step` is one transition,
`Run a b r` one application returning `r`; `run_iff_apply` says `Run` is `Apply`.
-/

namespace TreeCalculus

open Term

/-- A continuation on the loop's stack. -/
inductive Frame (α : Type) where
  /-- `APPLY_TO(arg)`: when the current reduction lands its result `r`, begin `apply(r, arg)`. -/
  | applyTo (arg : α)
  /-- `COMPUTE_AND_APPLY(fn, arg)`: when it lands `r`, push `APPLY_TO(r)` and begin
  `apply(fn, arg)` — the `apply(apply(fn, arg), r)` shape rule (2) reduces to. -/
  | computeAndApply (fn arg : α)

/-- A label of the loop, with the frames on its stack, top first. -/
inductive State (α : Type) where
  /-- `reduce` — evaluate `apply(a, b)`. -/
  | reduce (a b : α) (k : List (Frame α))
  /-- `dispatch` — feed the result `r` to the pending continuation. -/
  | dispatch (r : α) (k : List (Frame α))

def Frame.map (f : α → β) : Frame α → Frame β
  | .applyTo e => .applyTo (f e)
  | .computeAndApply g e => .computeAndApply (f g) (f e)

def State.map (f : α → β) : State α → State β
  | .reduce a b k => .reduce (f a) (f b) (k.map (·.map f))
  | .dispatch r k => .dispatch (f r) (k.map (·.map f))

/-- One transition: one at every `reduce`, and `none` at `dispatch` with no frame left to pop,
where the loop returns its result. -/
def step : State Value → Option (State Value)
  | .reduce a b k => some <| match a with
    | .leaf => .dispatch (.stem b) k                       -- apply(△, b) = △b
    | .stem u => .dispatch (.fork u b) k                   -- apply(△u, b) = △ub
    | .fork .leaf y => .dispatch y k                       -- apply(△△y, b) = y
    | .fork (.stem u') y => .reduce y b (.computeAndApply u' b :: k)
                                   -- apply(△(△u')y, b) = apply(apply(u', b), apply(y, b))
    | .fork (.fork w x) y => match b with                  -- apply(△(△wx)y, b) — triage on b
      | .leaf => .dispatch w k                             --   b = △:  w
      | .stem d => .reduce x d k                           --   b = △d: apply(x, d)
      | .fork d e => .reduce y d (.applyTo e :: k)         --   b = △de: apply(apply(y, d), e)
  | .dispatch _ [] => none
  | .dispatch r (.applyTo arg :: k) => some (.reduce r arg k)
  | .dispatch r (.computeAndApply fn arg :: k) => some (.reduce fn arg (.applyTo r :: k))

/-- Zero or more `r`-steps: `Steps`, for any relation. -/
inductive Star (r : α → α → Prop) : α → α → Prop
  | refl (t : α) : Star r t t
  | head : r s t → Star r t u → Star r s u

theorem Star.single (h : r s t) : Star r s t := .head h (.refl t)

theorem Star.trans (h₁ : Star r s t) (h₂ : Star r t u) : Star r s u := by
  induction h₁ with
  | refl => exact h₂
  | head hst _ ih => exact .head hst (ih h₂)

/-- `s` runs to `t` in zero or more steps. -/
abbrev Reaches : State Value → State Value → Prop := Star (step · = some ·)

/-- `apply(a, b)` returns `r`: from `reduce` with nothing pushed, the loop arrives at `dispatch`
with nothing left to pop. -/
def Run (a b r : Value) : Prop := Reaches (.reduce a b []) (.dispatch r [])

/-- `s` with `k` pushed underneath it. -/
def State.app (k : List (Frame α)) : State α → State α
  | .reduce a b k' => .reduce a b (k' ++ k)
  | .dispatch r k' => .dispatch r (k' ++ k)

/-- A step reads and pushes only frames above the stack it started from. -/
theorem step_app (h : step s = some s') (k) : step (s.app k) = some (s'.app k) := by
  match s with
  | .reduce a b _ =>
    rcases a with _ | _ | ⟨_ | _ | _, _⟩ <;> (try rcases b with _ | _ | _) <;> cases h <;> rfl
  | .dispatch _ [] => cases h
  | .dispatch _ (.applyTo _ :: _) => cases h; rfl
  | .dispatch _ (.computeAndApply _ _ :: _) => cases h; rfl

theorem Reaches.app (h : Reaches s t) (k) : Reaches (s.app k) (t.app k) := by
  induction h with
  | refl => exact .refl _
  | head hs _ ih => exact .head (step_app hs k) ih

/-! ## Agreement with the reduction rules -/

/-- `toTerm` is injective. -/
theorem Value.toTerm_inj : {a b : Value} → a.toTerm = b.toTerm → a = b
  | .leaf, .leaf, _ => rfl
  | .stem _, .stem _, h => by injection h with _ h; rw [toTerm_inj h]
  | .fork _ _, .fork _ _, h => by
    injection h with h h₂; injection h with _ h₁; rw [toTerm_inj h₁, toTerm_inj h₂]

/-- `toTerm` lands on values. -/
theorem Value.isValue : (v : Value) → IsValue v.toTerm
  | .leaf => .leaf
  | .stem x => .stem x.isValue
  | .fork x y => .fork x.isValue y.isValue

/-- An `Apply` of two values is a run of the machine, whatever stack is under it, to a value. -/
theorem reaches_of_apply (h : Apply A B R) : ∀ {a b : Value}, a.toTerm = A → b.toTerm = B →
    ∃ r : Value, r.toTerm = R ∧ ∀ k, Reaches (.reduce a b k) (.dispatch r k) := by
  induction h with
  | underLeaf => rintro (_ | _ | _) b ⟨⟩ rfl; exact ⟨.stem b, rfl, fun _ => .single rfl⟩
  | underStem => rintro (_ | u | _) b ⟨⟩ rfl; exact ⟨.fork u b, rfl, fun _ => .single rfl⟩
  | k => rintro (_ | _ | ⟨_ | _ | _, y⟩) b ⟨⟩ rfl; exact ⟨y, rfl, fun _ => .single rfl⟩
  | s _ _ _ ih₁ ih₂ ih₃ =>
    rintro (_ | _ | ⟨_ | _ | _, _⟩) b ⟨⟩ rfl
    obtain ⟨_, rfl, h₁⟩ := ih₁ rfl rfl
    obtain ⟨_, rfl, h₂⟩ := ih₂ rfl rfl
    obtain ⟨v, rfl, h₃⟩ := ih₃ rfl rfl
    exact ⟨v, rfl, fun k => .head rfl <| (h₂ _).trans <| .head rfl <| (h₁ _).trans <|
      .head rfl (h₃ k)⟩
  | fLeaf =>
    rintro (_ | _ | ⟨_ | _ | ⟨w, _⟩, _⟩) (_ | _ | _) ⟨⟩ ⟨⟩
    exact ⟨w, rfl, fun _ => .single rfl⟩
  | fStem _ ih =>
    rintro (_ | _ | ⟨_ | _ | _, _⟩) (_ | _ | _) ⟨⟩ ⟨⟩
    obtain ⟨v, rfl, h⟩ := ih rfl rfl
    exact ⟨v, rfl, fun k => .head rfl (h k)⟩
  | fFork _ _ ih₁ ih₂ =>
    rintro (_ | _ | ⟨_ | _ | _, _⟩) (_ | _ | _) ⟨⟩ ⟨⟩
    obtain ⟨_, rfl, h₁⟩ := ih₁ rfl rfl
    obtain ⟨r, rfl, h₂⟩ := ih₂ rfl rfl
    exact ⟨r, rfl, fun k => .head rfl <| (h₁ _).trans <| .head rfl (h₂ k)⟩

/-- `reaches_of_apply`, for values. -/
theorem Term.Apply.reaches (h : Apply a.toTerm b.toTerm r.toTerm) (k) :
    Reaches (.reduce a b k) (.dispatch r k) := by
  obtain ⟨_, e, h⟩ := reaches_of_apply h rfl rfl; cases Value.toTerm_inj e; exact h k

/-- What a stack does to the result fed to it, in `Apply`'s terms. -/
def Cont : List (Frame Value) → Value → Value → Prop
  | [], r, r' => r = r'
  | .applyTo e :: k, r, r' => ∃ v : Value, Apply r.toTerm e.toTerm v.toTerm ∧ Cont k v r'
  | .computeAndApply f e :: k, r, r' =>
    ∃ v w : Value, Apply f.toTerm e.toTerm v.toTerm ∧ Apply v.toTerm r.toTerm w.toTerm ∧ Cont k w r'

/-- What a state goes on to return, in `Apply`'s terms. -/
def Sem : State Value → Value → Prop
  | .reduce a b k, r' => ∃ v : Value, Apply a.toTerm b.toTerm v.toTerm ∧ Cont k v r'
  | .dispatch r k, r' => Cont k r r'

theorem step_sem (h : step s = some s') (h' : Sem s' r') : Sem s r' := by
  match s, h with
  | .reduce .leaf _ _, rfl => exact ⟨.stem _, .underLeaf, h'⟩
  | .reduce (.stem _) _ _, rfl => exact ⟨.fork _ _, .underStem, h'⟩
  | .reduce (.fork .leaf _) _ _, rfl => exact ⟨_, .k, h'⟩
  | .reduce (.fork (.stem _) _) _ _, rfl =>
    obtain ⟨_, e₁, _, _, e₂, e₃, c⟩ := h'; exact ⟨_, .s e₂ e₁ e₃, c⟩
  | .reduce (.fork (.fork _ _) _) .leaf _, rfl => exact ⟨_, .fLeaf, h'⟩
  | .reduce (.fork (.fork _ _) _) (.stem _) _, rfl =>
    obtain ⟨_, e, c⟩ := h'; exact ⟨_, .fStem e, c⟩
  | .reduce (.fork (.fork _ _) _) (.fork _ _) _, rfl =>
    obtain ⟨_, e₁, _, e₂, c⟩ := h'; exact ⟨_, .fFork e₁ e₂, c⟩
  | .dispatch _ (.applyTo _ :: _), rfl => exact h'
  | .dispatch _ (.computeAndApply _ _ :: _), rfl =>
    obtain ⟨_, e₁, _, e₂, c⟩ := h'; exact ⟨_, _, e₁, e₂, c⟩

theorem Reaches.sem (h : Reaches s (.dispatch r [])) : Sem s r := by
  generalize ht : State.dispatch r [] = t at h
  induction h with
  | refl => cases ht; rfl
  | head hs _ ih => exact step_sem hs (ih ht)

/-- The machine computes exactly eager application. -/
theorem run_iff_apply : Run a b r ↔ Apply a.toTerm b.toTerm r.toTerm :=
  ⟨fun h => by obtain ⟨_, e, rfl⟩ := h.sem; exact e, fun h => h.reaches []⟩

end TreeCalculus
