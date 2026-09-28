import TreeCalculus.Machine

/-!
# What the runtime adds: the memo, and jets

`step` is the reduction loop alone. Around it the runtime memoizes — `MEMOIZE` frames and the
memo they fill — and, the point of `Check.lean`, may take jets: answer, or skip ahead in, a
reduction it recognizes without running it. Both are shortcuts. This is the machine with both:
a memo that may hold or forget anything, which covers every admission and eviction policy at
once (the direct-mapped overwrite, `MEMO_MIN_STEPS`, a collection's filter), and jets as any
relation `J` on states each of whose transitions is a composite of real steps (`Reaches`) —
which is what a checker's theorems establish (`MemberJet.reaches`, `DropJet.reaches`).
`runtime_sound`: it returns what `Run` says, and fills the memo only with entries `Run` agrees
with.
-/

namespace TreeCalculus

/-- What the memo can answer: `m a b r` is an entry `apply(a, b) = r`. -/
abbrev Memo := Tree → Tree → Tree → Prop

/-- `apply`'s loop with the memo. The stack is cut at its `MEMOIZE` frames: `s` holds the frames
above the topmost, and `memos` each `MEMOIZE(a, b)`, top first, with the frames between it and
the next one down. -/
structure RState where
  s : State Tree
  memos : List (Tree × Tree × List (Frame Tree))
  memo : Memo

inductive RStep (J : State Tree → State Tree → Prop) : RState → RState → Prop
  /-- Any rule, above the topmost `MEMOIZE`. -/
  | step : step s = some s' → RStep J ⟨s, fs, m⟩ ⟨s', fs, m⟩
  /-- A jet. -/
  | jet : J s s' → RStep J ⟨s, fs, m⟩ ⟨s', fs, m⟩
  /-- `memo_get(a, b)` hits: `result = hit; goto dispatch`. -/
  | hit : m a b r → RStep J ⟨.reduce a b k, fs, m⟩ ⟨.dispatch r k, fs, m⟩
  /-- It misses: `_stack.push_back({MEMOIZE, a, b})`, underneath what the rule pushes. -/
  | miss : RStep J ⟨.reduce a b k, fs, m⟩ ⟨.reduce a b [], (a, b, k) :: fs, m⟩
  /-- `MEMOIZE(a, b)` pops: `memo_put(a, b, result)`. -/
  | put : RStep J ⟨.dispatch r [], (a, b, k) :: fs, m⟩
      ⟨.dispatch r k, fs, fun a' b' r' => m a' b' r' ∨ a' = a ∧ b' = b ∧ r' = r⟩
  /-- Entries go: overwritten, never let in, or filtered out by a collection. -/
  | forget : (∀ a b r, m' a b r → m a b r) → RStep J ⟨s, fs, m⟩ ⟨s, fs, m'⟩

/-- Each `MEMOIZE(a, b)` frame's `reduce a b` has so far run to the state above that frame,
and the call at the bottom has run from `start` to the whole. -/
def Pending (start : State Tree) : State Tree → List (Tree × Tree × List (Frame Tree)) → Prop
  | s, [] => Reaches start s
  | s, (a, b, k) :: fs => Reaches (.reduce a b []) s ∧ Pending start (s.app k) fs

theorem Pending.reaches (h : Reaches s s') (hp : Pending start s fs) : Pending start s' fs := by
  induction fs generalizing s s' with
  | nil => exact hp.trans h
  | cons f _ ih => obtain ⟨_, _, k⟩ := f; exact ⟨hp.1.trans h, ih (h.app k) hp.2⟩

/-- Every entry is one `Run` agrees with. -/
def Memo.Sound (m : Memo) : Prop := ∀ a b r, m a b r → Run a b r

theorem RStep.pending (hj : ∀ s t, J s t → Reaches s t) (h : RStep J x y)
    (hp : Pending start x.s x.memos) (hm : x.memo.Sound) :
    Pending start y.s y.memos ∧ y.memo.Sound := by
  cases h with
  | step hs => exact ⟨hp.reaches (.single hs), hm⟩
  | jet hs => exact ⟨hp.reaches (hj _ _ hs), hm⟩
  | hit hr => exact ⟨hp.reaches ((hm _ _ _ hr).frame _), hm⟩
  | miss => exact ⟨⟨.refl, by simpa [State.app] using hp⟩, hm⟩
  | put =>
    refine ⟨by simpa [State.app] using hp.2, ?_⟩
    rintro _ _ _ (h | ⟨rfl, rfl, rfl⟩)
    · exact hm _ _ _ h
    · exact hp.1
  | forget hf => exact ⟨hp, fun a b r h => hm a b r (hf a b r h)⟩

/-- With the memo and jets, `apply(a, b)` still returns what `Run` says, and leaves the memo
sound. -/
theorem runtime_sound (hj : ∀ s t, J s t → Reaches s t) (hm : m.Sound)
    (h : Star (RStep J) ⟨.reduce a b [], [], m⟩ ⟨.dispatch r [], [], m'⟩) :
    Run a b r ∧ m'.Sound := by
  suffices ∀ x y, Star (RStep J) x y → Pending (.reduce a b []) x.s x.memos → x.memo.Sound →
      Pending (.reduce a b []) y.s y.memos ∧ y.memo.Sound from this _ _ h .refl hm
  intro x y h; induction h with
  | refl => exact fun hp hm => ⟨hp, hm⟩
  | cons hs _ ih => exact fun hp hm => let ⟨hp', hm'⟩ := hs.pending hj hp hm; ih hp' hm'

end TreeCalculus
