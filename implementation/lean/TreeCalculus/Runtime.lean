import TreeCalculus.Machine
import TreeCalculus.StrongNormalization

/-!
# A runtime: the machine with a memo and jets

`RStep J` is `step` with a memo it may hit, fill and forget under any policy, and with jets: any
transitions `J`. `runtime_sound`: if every `J`-transition `Reaches`, the runtime returns what
`Apply` says and keeps the memo sound.
-/

namespace TreeCalculus

open Term

/-- What the memo can answer: `m a b r` is an entry `apply(a, b) = r`. -/
abbrev Memo := Value → Value → Value → Prop

/-- The loop with a memo. The stack is cut at its memo frames: `s` holds the frames above the
topmost, and `memos` each memo frame's `a` and `b`, top first, with the frames between it and the
next one down. -/
structure RState where
  s : State Value
  memos : List (Value × Value × List (Frame Value))
  memo : Memo

inductive RStep (J : State Value → State Value → Prop) : RState → RState → Prop
  /-- Any rule, above the topmost memo frame. -/
  | step : step s = some s' → RStep J ⟨s, fs, m⟩ ⟨s', fs, m⟩
  /-- A jet. -/
  | jet : J s s' → RStep J ⟨s, fs, m⟩ ⟨s', fs, m⟩
  /-- The memo answers `apply(a, b)`. -/
  | hit : m a b r → RStep J ⟨.reduce a b k, fs, m⟩ ⟨.dispatch r k, fs, m⟩
  /-- It does not: a memo frame for `a` and `b`, underneath what the rule pushes. -/
  | miss : RStep J ⟨.reduce a b k, fs, m⟩ ⟨.reduce a b [], (a, b, k) :: fs, m⟩
  /-- A memo frame pops: its entry `apply(a, b) = r` goes in. -/
  | put : RStep J ⟨.dispatch r [], (a, b, k) :: fs, m⟩
      ⟨.dispatch r k, fs, fun a' b' r' => m a' b' r' ∨ a' = a ∧ b' = b ∧ r' = r⟩
  /-- Entries go: overwritten, never let in, or dropped — by any policy at all. -/
  | forget : (∀ a b r, m' a b r → m a b r) → RStep J ⟨s, fs, m⟩ ⟨s, fs, m'⟩

/-- Each memo frame's `reduce a b` has so far run to the state above that frame, and the
application at the bottom has run from `start` to the whole. -/
def Pending (start : State Value) : State Value → List (Value × Value × List (Frame Value)) → Prop
  | s, [] => Reaches start s
  | s, (a, b, k) :: fs => Reaches (.reduce a b []) s ∧ Pending start (s.app k) fs

theorem Pending.reaches (h : Reaches s s') (hp : Pending start s fs) : Pending start s' fs := by
  induction fs generalizing s s' with
  | nil => exact hp.trans h
  | cons f _ ih => obtain ⟨_, _, k⟩ := f; exact ⟨hp.1.trans h, ih (h.app k) hp.2⟩

/-- Every entry is one `Apply` agrees with. -/
def Memo.Sound (m : Memo) : Prop := ∀ a b r, m a b r → Apply a.toTerm b.toTerm r.toTerm

theorem RStep.pending (hj : ∀ s t, J s t → Reaches s t) (h : RStep J x y)
    (hp : Pending start x.s x.memos) (hm : x.memo.Sound) :
    Pending start y.s y.memos ∧ y.memo.Sound := by
  cases h with
  | step hs => exact ⟨hp.reaches (.single hs), hm⟩
  | jet hs => exact ⟨hp.reaches (hj _ _ hs), hm⟩
  | hit hr => exact ⟨hp.reaches ((hm _ _ _ hr).reaches _), hm⟩
  | miss => exact ⟨⟨.refl _, by simpa [State.app] using hp⟩, hm⟩
  | put =>
    refine ⟨by simpa [State.app] using hp.2, ?_⟩
    rintro _ _ _ (h | ⟨rfl, rfl, rfl⟩)
    · exact hm _ _ _ h
    · exact run_iff_apply.1 hp.1
  | forget hf => exact ⟨hp, fun a b r h => hm a b r (hf a b r h)⟩

/-- With the memo and jets, `apply(a, b)` still returns what `Apply` says, and leaves the memo
sound. -/
theorem runtime_sound (hj : ∀ s t, J s t → Reaches s t) (hm : m.Sound)
    (h : Star (RStep J) ⟨.reduce a b [], [], m⟩ ⟨.dispatch r [], [], m'⟩) :
    Apply a.toTerm b.toTerm r.toTerm ∧ m'.Sound := by
  refine .imp_left run_iff_apply.1 ?_
  suffices ∀ x y, Star (RStep J) x y → Pending (.reduce a b []) x.s x.memos → x.memo.Sound →
      Pending (.reduce a b []) y.s y.memos ∧ y.memo.Sound from this _ _ h (.refl _) hm
  intro x y h; induction h with
  | refl => exact fun hp hm => ⟨hp, hm⟩
  | head hs _ ih => exact fun hp hm => let ⟨hp', hm'⟩ := hs.pending hj hp hm; ih hp' hm'

/-- End to end: `a ⬝ b` is strongly normalizing — no strategy diverges on it — and what the
runtime returns is a value it reduces to. -/
example (hj : ∀ s t, J s t → Reaches s t) (hm : m.Sound)
    (h : Star (RStep J) ⟨.reduce a b [], [], m⟩ ⟨.dispatch r [], [], m'⟩) :
    SN (a.toTerm ⬝ b.toTerm) ∧ Steps (a.toTerm ⬝ b.toTerm) r.toTerm ∧ IsValue r.toTerm :=
  (Eval.app a.isValue.eval_self b.isValue.eval_self (runtime_sound hj hm h).1).sn_steps_value

end TreeCalculus
