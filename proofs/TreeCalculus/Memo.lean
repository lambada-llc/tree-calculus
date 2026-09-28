import TreeCalculus.Machine

/-!
# The memo, put back

`step` leaves out `MEMOIZE` frames and the memo they fill. This puts them back — as a machine
whose memo may hold or forget anything, which covers every eviction and admission policy at once
(the direct-mapped overwrite, `MEMO_MIN_STEPS`, a collection's filter) — and proves it computes
exactly what `Run` says, filling the memo only with entries `Run` agrees with.
-/

namespace TreeCalculus

/-- What the memo can answer: `m a b r` is an entry `apply(a, b) = r`. -/
abbrev Memo := Tree → Tree → Tree → Prop

/-- `apply`'s loop with the memo. The stack is cut at its `MEMOIZE` frames: `s` holds the frames
above the topmost, and `memos` each `MEMOIZE(a, b)`, top first, with the frames between it and
the next one down. -/
structure MState where
  s : State Tree
  memos : List (Tree × Tree × List (Frame Tree))
  memo : Memo

inductive MStep : MState → MState → Prop
  /-- Any rule, above the topmost `MEMOIZE`. -/
  | step : step s = some s' → MStep ⟨s, fs, m⟩ ⟨s', fs, m⟩
  /-- `memo_get(a, b)` hits: `result = hit; goto dispatch`. -/
  | hit : m a b r → MStep ⟨.reduce a b k, fs, m⟩ ⟨.dispatch r k, fs, m⟩
  /-- It misses: `_stack.push_back({MEMOIZE, a, b})`, underneath what the rule pushes. -/
  | miss : MStep ⟨.reduce a b k, fs, m⟩ ⟨.reduce a b [], (a, b, k) :: fs, m⟩
  /-- `MEMOIZE(a, b)` pops: `memo_put(a, b, result)`. -/
  | put : MStep ⟨.dispatch r [], (a, b, k) :: fs, m⟩
      ⟨.dispatch r k, fs, fun a' b' r' => m a' b' r' ∨ a' = a ∧ b' = b ∧ r' = r⟩
  /-- Entries go: overwritten, not admitted, or filtered out by a collection. -/
  | forget : (∀ a b r, m' a b r → m a b r) → MStep ⟨s, fs, m⟩ ⟨s, fs, m'⟩

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

theorem MStep.pending (h : MStep x y) (hp : Pending start x.s x.memos) (hm : x.memo.Sound) :
    Pending start y.s y.memos ∧ y.memo.Sound := by
  cases h with
  | step hs => exact ⟨hp.reaches (.single hs), hm⟩
  | hit hr => exact ⟨hp.reaches ((hm _ _ _ hr).frame _), hm⟩
  | miss => exact ⟨⟨.refl, by simpa [State.app] using hp⟩, hm⟩
  | put =>
    refine ⟨by simpa [State.app] using hp.2, ?_⟩
    rintro _ _ _ (h | ⟨rfl, rfl, rfl⟩)
    · exact hm _ _ _ h
    · exact hp.1
  | forget hf => exact ⟨hp, fun a b r h => hm a b r (hf a b r h)⟩

/-- With the memo, `apply(a, b)` still returns what `Run` says, and leaves the memo sound. -/
theorem memo_sound (hm : m.Sound)
    (h : Star MStep ⟨.reduce a b [], [], m⟩ ⟨.dispatch r [], [], m'⟩) : Run a b r ∧ m'.Sound := by
  suffices ∀ x y, Star MStep x y → Pending (.reduce a b []) x.s x.memos → x.memo.Sound →
      Pending (.reduce a b []) y.s y.memos ∧ y.memo.Sound from this _ _ h .refl hm
  intro x y h; induction h with
  | refl => exact fun hp hm => ⟨hp, hm⟩
  | cons hs _ ih => exact fun hp hm => let ⟨hp', hm'⟩ := hs.pending hp hm; ih hp' hm'

end TreeCalculus
