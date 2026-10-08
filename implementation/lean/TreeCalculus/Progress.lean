import TreeCalculus.RuleJets

/-!
# Progress: what jets and the memo do to termination

`runtime_sound` (`Runtime.lean`) asks of a jet only `Reaches`: zero or more machine steps. That is
partial correctness. This module settles what it says about termination.

* `Reaches.eq_or_plus`: a `Reaches`-jet that is not a `Plus`-jet (one step or more) differs from
  one only at `J s s`, a no-op.
* `RStep` itself is never well-founded, for any `J`, even none: `forget` may keep the memo as it is
  (`RStep.not_acc`), and without `forget`, `miss` may push frames forever (`RStep.miss_not_acc`).
  Termination is a property of a *policy* (`Policy`: a live `P ⊆ RStep J` that idles finitely).
* `RStep.progress`: with `Plus`-jets and a sound memo, every transition moves the *whole* state
  (`RState.whole`: the stack with the memo frames glued back in) by `Plus`, or is bookkeeping
  (`Book`: `miss`, `put`, `forget`), which leaves the whole state as it is. With `Reaches`-jets,
  the one more way to not move it is a jet at `J s s` (`RStep.progress_reaches`).
* `machine_returns_of_runtime` (only `Reaches`-jets): a runtime that returns `r` is a machine that
  returns `r` — no jet rescues a diverging program.
* `runtime_returns_iff` / `runtime_terminates_iff`: for any policy and `Reaches`-jets, the runtime
  returns `r` iff the machine does, and all its runs are finite iff the machine returns.
* `runtime_moves_le`: if the machine returns in `N` steps, no run of the runtime (any interleaving
  at all) makes more than `N` transitions that move the machine.
* `memoPolicy`: a concrete policy — any interleaving of step, jet, hit, put, and a non-redundant
  miss; no `forget` — idles finitely by a measure, *provided the jets are `Plus`-jets*: that is
  where `Plus` is needed (`memoPolicy_returns_iff`).
* `checkRule_plus`, `checkRule_zero`: a checked rule takes a step on every instance unless its length
  is `0` (its sides are equal); `DropJet.plus`: the drop-through jet does (`Rules.fix_plus`: so do
  the fix rules).
* `Peek`: `S K y b ⟶ b` is term-sound, is not a `Reaches`-jet, and returns where the machine
  diverges.
-/

namespace TreeCalculus

open Term SValue

/-! ## The machine: determinism -/

/-- At least one step. -/
def Plus (s u : State Value) : Prop := ∃ t, step s = some t ∧ Reaches t u

theorem Plus.reaches : Plus s u → Reaches s u
  | ⟨_, h, h'⟩ => .head h h'

theorem Reaches.eq_or_plus : Reaches s t → s = t ∨ Plus s t
  | .refl _ => .inl rfl
  | .head h h' => .inr ⟨_, h, h'⟩

theorem Reaches.plus (h : Reaches s t) (hne : s ≠ t) : Plus s t := h.eq_or_plus.resolve_left hne

theorem Plus.trans (h : Plus s t) (h' : Reaches t u) : Plus s u :=
  let ⟨x, h₁, h₂⟩ := h; ⟨x, h₁, h₂.trans h'⟩

theorem Plus.app (h : Plus s t) (k) : Plus (s.app k) (t.app k) :=
  let ⟨_, h₁, h₂⟩ := h; ⟨_, step_app h₁ k, h₂.app k⟩

/-- `dispatch r []` takes no step: the loop returns. -/
theorem step_dispatch_nil : step (.dispatch r []) = none := rfl

/-- Determinism: whatever `s` reaches lies on its run to a halted `t`. -/
theorem Reaches.det (h : Reaches s t) (ht : step t = none) (h' : Reaches s u) : Reaches u t := by
  induction h generalizing u with
  | refl => cases h' with
    | refl => exact .refl _
    | head hs _ => rw [ht] at hs; cases hs
  | head hs hr ih => cases h' with
    | refl => exact .head hs hr
    | head hs' hu => rw [hs] at hs'; cases hs'; exact ih ht hu

/-- A state reaches at most one returned result. -/
theorem Reaches.result_unique (h : Reaches s (.dispatch r [])) (h' : Reaches s (.dispatch r' [])) :
    r = r' := by
  cases (h.det rfl h') with
  | refl => rfl
  | head hs _ => cases hs

/-- `t` is strictly later than `s` on the run. -/
def Later (t s : State Value) : Prop := Plus s t

/-- A state whose run halts has a finite future: `Later` is well-founded on it. -/
theorem Reaches.acc_later (h : Reaches s t) (ht : step t = none) : Acc Later s := by
  induction h with
  | refl => exact ⟨_, fun _ ⟨_, hx, _⟩ => by rw [ht] at hx; cases hx⟩
  | head hs _ ih =>
    refine ⟨_, fun y ⟨x, hx, hxy⟩ => ?_⟩
    rw [hs] at hx; cases hx
    rcases hxy.eq_or_plus with rfl | p
    · exact ih ht
    · exact (ih ht).inv p

theorem acc_not_self (h : Acc r x) (hx : r x x) : False := by
  induction h with
  | intro x _ ih => exact ih x hx hx

/-- A state on a cycle never returns. -/
theorem Plus.cycle_no_return (hc : Plus s s) (hr : Reaches s (.dispatch r [])) : False :=
  acc_not_self (hr.acc_later rfl) hc

/-! ## The runtime: the whole state, and the transitions that do not move it -/

/-- The stack with the memo frames glued back in: `s` above, then each memo frame's `k`. -/
def glue : State Value → List (Value × Value × List (Frame Value)) → State Value
  | s, [] => s
  | s, (_, _, k) :: fs => glue (s.app k) fs

/-- The machine state the runtime state stands for. -/
def RState.whole (x : RState) : State Value := glue x.s x.memos

theorem step_glue (h : step s = some s') : ∀ fs, step (glue s fs) = some (glue s' fs)
  | [] => h
  | (_, _, k) :: fs => step_glue (step_app h k) fs

theorem Reaches.glue (h : Reaches s t) (fs) : Reaches (glue s fs) (glue t fs) := by
  induction h with
  | refl => exact .refl _
  | head hs _ ih => exact .head (step_glue hs fs) ih

theorem Plus.glue (h : Plus s t) (fs) : Plus (glue s fs) (glue t fs) :=
  let ⟨_, h₁, h₂⟩ := h; ⟨_, step_glue h₁ fs, h₂.glue fs⟩

theorem Pending.whole : ∀ {s fs}, Pending start s fs → Reaches start (glue s fs)
  | _, [], h => h
  | s, (_, _, k) :: fs, h => (Pending.whole h.2 : Reaches start (glue (s.app k) fs))

/-- Bookkeeping: the memo's own transitions, `RStep`'s `miss`, `put` and `forget`. -/
inductive Book : RState → RState → Prop
  | miss : Book ⟨.reduce a b k, fs, m⟩ ⟨.reduce a b [], (a, b, k) :: fs, m⟩
  | put : Book ⟨.dispatch r [], (a, b, k) :: fs, m⟩
      ⟨.dispatch r k, fs, fun a' b' r' => m a' b' r' ∨ a' = a ∧ b' = b ∧ r' = r⟩
  | forget : (∀ a b r, m' a b r → m a b r) → Book ⟨s, fs, m⟩ ⟨s, fs, m'⟩

theorem Book.rstep : Book x y → RStep J x y
  | .miss => .miss
  | .put => .put
  | .forget h => .forget h

/-- Bookkeeping moves nothing on the machine. -/
theorem Book.whole : Book x y → y.whole = x.whole
  | .miss | .put => by simp [RState.whole, glue, State.app]
  | .forget _ => rfl

/-- **The core lemma.** With `Plus`-jets and a sound memo, a transition either moves the whole
state at least one machine step forward, or is bookkeeping. -/
theorem RStep.progress (hj : ∀ s t, J s t → Plus s t) (hm : x.memo.Sound) :
    RStep J x y → Plus x.whole y.whole ∨ Book x y
  | .step hs => .inl (Plus.glue ⟨_, hs, .refl _⟩ _)
  | .jet hs => .inl ((hj _ _ hs).glue _)
  | .hit hr => .inl ((((hm _ _ _ hr).reaches _).plus (by simp)).glue _)
  | .miss => .inr .miss
  | .put => .inr .put
  | .forget h => .inr (.forget h)

/-- The same with `Reaches`-jets: the one thing added is a jet that fires `J s s`. -/
theorem RStep.progress_reaches (hj : ∀ s t, J s t → Reaches s t) (hm : x.memo.Sound) :
    RStep J x y → Plus x.whole y.whole ∨ Book x y ∨ (J x.s x.s ∧ y = x)
  | .step hs => .inl (Plus.glue ⟨_, hs, .refl _⟩ _)
  | .jet hs => by
    rcases (hj _ _ hs).eq_or_plus with rfl | p
    · exact .inr (.inr ⟨hs, rfl⟩)
    · exact .inl (p.glue _)
  | .hit hr => .inl ((((hm _ _ _ hr).reaches _).plus (by simp)).glue _)
  | .miss => .inr (.inl .miss)
  | .put => .inr (.inl .put)
  | .forget h => .inr (.inl (.forget h))

/-- With `Reaches`-jets and a sound memo, a transition that does not move the machine leaves the
whole state as it is (it is bookkeeping, or a jet fired at `J s s`). -/
theorem RStep.whole_eq (hj : ∀ s t, J s t → Reaches s t) (hm : x.memo.Sound) (h : RStep J x y)
    (hp : ¬ Plus x.whole y.whole) : y.whole = x.whole := by
  rcases h.progress_reaches hj hm with p | bk | ⟨_, rfl⟩
  · exact absurd p hp
  · exact bk.whole
  · rfl

/-! ## Counterexamples: `RStep` itself never terminates -/

/-- `forget` may keep the memo as it is: every runtime state has an infinite run, for any `J`. -/
theorem RStep.not_acc : ¬ Acc (fun y x => RStep J x y) x :=
  fun h => acc_not_self h (.forget fun _ _ _ => id)

/-- Without `forget`, and without any jet, `miss` alone runs forever from every `reduce`. -/
theorem RStep.miss_not_acc :
    ¬ Acc (fun y x => RStep (fun _ _ => False) x y ∧ y.memos ≠ x.memos) ⟨.reduce a b k, fs, m⟩ := by
  intro h
  generalize hx : (⟨.reduce a b k, fs, m⟩ : RState) = x at h
  induction h generalizing k fs with
  | intro x _ ih =>
    subst hx
    exact ih _ ⟨.miss, fun e => by simp at e⟩ rfl

/-- A `Reaches`-jet may stutter: `J = Eq` meets `runtime_sound`'s hypothesis and loops. -/
theorem RStep.eq_jet_loop : RStep (· = ·) x x := .jet rfl

theorem eq_jet_reaches : ∀ s t, (s = t) → Reaches s t := fun _ _ h => h ▸ .refl _

/-! ## Termination under a policy -/

/-- `x` has returned. -/
def RState.Done (x : RState) : Prop := ∃ r, x.s = .dispatch r [] ∧ x.memos = []

/-- What `runtime_sound` keeps along a run from `start`. -/
def RInv (start : State Value) (x : RState) : Prop := Pending start x.s x.memos ∧ x.memo.Sound

theorem RStep.rinv (hj : ∀ s t, J s t → Reaches s t) (h : RStep J x y) (hx : RInv start x) :
    RInv start y := h.pending hj hx.1 hx.2

theorem Star.rinv (hj : ∀ s t, J s t → Reaches s t) (hP : ∀ x y, P x y → RStep J x y)
    (h : Star P x y) (hx : RInv start x) : RInv start y := by
  induction h with
  | refl => exact hx
  | head hs _ ih => exact ih ((hP _ _ hs).rinv hj hx)

theorem Star.tail (h : Star r s t) (h' : r t u) : Star r s u := h.trans (.single h')

/-- The runtime state a run of `apply(a, b)` starts in. -/
abbrev RState.init (a b : Value) (m : Memo) : RState := ⟨.reduce a b [], [], m⟩

theorem RInv.init (hm : m.Sound) : RInv (.reduce a b []) (RState.init a b m) := ⟨.refl _, hm⟩

/-- **Jets cannot make a diverging program return** (`Reaches`-jets suffice): a run of the
runtime to `dispatch r []` is a run of the machine to the same `r`. A corollary of
`runtime_sound` and `run_iff_apply`. -/
theorem machine_returns_of_runtime (hj : ∀ s t, J s t → Reaches s t) (hm : m.Sound)
    (h : Star (RStep J) (RState.init a b m) ⟨.dispatch r [], [], m'⟩) : Run a b r :=
  run_iff_apply.2 (runtime_sound hj hm h).1

/-- A policy: which transitions a runtime takes. `P ⊆ RStep J`; it may be nondeterministic. -/
structure Policy (J : State Value → State Value → Prop) (P : RState → RState → Prop)
    (a b : Value) (m : Memo) : Prop where
  /-- Every transition the policy takes is one of `RStep`'s. -/
  sub : ∀ x y, P x y → RStep J x y
  /-- It is never stuck before returning. -/
  live : ∀ x, Star P (RState.init a b m) x → ¬ x.Done → ∃ y, P x y
  /-- It does not idle forever: before returning, the transitions it takes that do not move the
  machine (`¬ Plus` on the whole state) admit no infinite run. -/
  idle : ∀ x, Star P (RState.init a b m) x →
    Acc (fun y x => P x y ∧ ¬ Plus x.whole y.whole ∧ ¬ x.Done) x

theorem Policy.star (pol : Policy J P a b m) : Star P x y → Star (RStep J) x y := fun h => by
  induction h with
  | refl => exact .refl _
  | head hs _ ih => exact .head (pol.sub _ _ hs) ih

/-- **Termination is preserved.** With `Reaches`-jets, a sound memo and a policy, if the machine
returns then the policy's runs are finite until they return: `P`, from states that have not
returned, is well-founded on everything the policy reaches. The measure is lexicographic: the
whole state's position on the machine's run (`Later`; a transition that does not advance it
leaves it as it is, `RStep.whole_eq`), then the policy's idling (`Policy.idle`). -/
theorem Policy.acc (pol : Policy J P a b m) (hj : ∀ s t, J s t → Reaches s t) (hm : m.Sound)
    (hrun : Run a b r) (hx : Star P (RState.init a b m) x) :
    Acc (fun y x => P x y ∧ ¬ x.Done) x := by
  have inv : ∀ {x}, Star P (RState.init a b m) x → RInv (.reduce a b []) x :=
    fun hx => hx.rinv hj pol.sub (RInv.init hm)
  have later : Acc Later x.whole := (hrun.det rfl (inv hx).1.whole).acc_later rfl
  generalize hg : x.whole = g at later
  induction later generalizing x with
  | intro g _ ihg =>
    have hi := pol.idle x hx
    induction hi with
    | intro x _ ihi =>
      refine ⟨_, fun y ⟨hxy, hnd⟩ => ?_⟩
      by_cases p : Plus x.whole y.whole
      · exact ihg _ (hg ▸ p) (hx.tail hxy) rfl
      · exact ihi y ⟨hxy, p, hnd⟩ (hx.tail hxy) (((pol.sub _ _ hxy).whole_eq hj (inv hx).2 p).trans hg)

/-- A finite, live run ends returned. -/
theorem Policy.done (pol : Policy J P a b m) (hx : Star P (RState.init a b m) x)
    (hacc : Acc (fun y x => P x y ∧ ¬ x.Done) x) : ∃ y, Star P x y ∧ y.Done := by
  induction hacc with
  | intro x _ ih =>
    by_cases hd : x.Done
    · exact ⟨x, .refl _, hd⟩
    · obtain ⟨y, hxy⟩ := pol.live x hx hd
      obtain ⟨z, hyz, hz⟩ := ih y ⟨hxy, hd⟩ (hx.tail hxy)
      exact ⟨z, .head hxy hyz, hz⟩

/-- **The runtime returns `r` iff the machine does.** For `Reaches`-jets, a sound memo, and any
policy that is live and idles finitely. (`→` needs no policy: `machine_returns_of_runtime`.)
Whether a policy idles finitely is where `Plus` comes in: see `memoPolicy`. -/
theorem runtime_returns_iff (pol : Policy J P a b m) (hj : ∀ s t, J s t → Reaches s t)
    (hm : m.Sound) :
    (∃ m', Star P (RState.init a b m) ⟨.dispatch r [], [], m'⟩) ↔ Run a b r := by
  refine ⟨fun ⟨_, h⟩ => machine_returns_of_runtime hj hm (pol.star h), fun hrun => ?_⟩
  obtain ⟨⟨s, memos, m'⟩, hy, r', hs, hmemos⟩ :=
    pol.done (.refl _) (pol.acc hj hm hrun (.refl _))
  simp only at hs hmemos; subst hs hmemos
  cases hrun.result_unique (machine_returns_of_runtime hj hm (pol.star hy))
  exact ⟨m', hy⟩

/-- And it diverges iff the machine does: every run of the policy is finite iff the machine
returns. (`←`: `Policy.acc`. `→`: a finite live run returns, and what it returns the machine
does.) -/
theorem runtime_terminates_iff (pol : Policy J P a b m) (hj : ∀ s t, J s t → Reaches s t)
    (hm : m.Sound) :
    Acc (fun y x => P x y ∧ ¬ x.Done) (RState.init a b m) ↔ ∃ r, Run a b r := by
  refine ⟨fun hacc => ?_, fun ⟨_, hrun⟩ => pol.acc hj hm hrun (.refl _)⟩
  obtain ⟨⟨s, memos, m'⟩, hy, r', hs, hmemos⟩ := pol.done (.refl _) hacc
  simp only at hs hmemos; subst hs hmemos
  exact ⟨r', machine_returns_of_runtime hj hm (pol.star hy)⟩

/-- Well-foundedness from a measure that decreases wherever `Q` holds, `Q` kept along `R`. -/
theorem acc_of_measure {R : α → α → Prop} (Q : α → Prop) (μ : α → Nat)
    (hQ : ∀ x y, Q x → R y x → Q y) (hμ : ∀ x y, Q x → R y x → μ y < μ x) (x : α) (hx : Q x) :
    Acc R x := by
  suffices ∀ n x, μ x < n → Q x → Acc R x from this _ x (Nat.lt_succ_self _) hx
  intro n; induction n with
  | zero => intro _ h; cases h
  | succ n ih =>
    intro x hn hx
    exact ⟨_, fun y hy => ih y (by have := hμ x y hx hy; omega) (hQ x y hx hy)⟩

/-! ## How many transitions move the machine, whatever the policy -/

/-- Exactly `n` steps. -/
inductive NSteps : Nat → State Value → State Value → Prop
  | refl (s : State Value) : NSteps 0 s s
  | head : step s = some t → NSteps n t u → NSteps (n + 1) s u

theorem NSteps.reaches : NSteps n s t → Reaches s t
  | .refl _ => .refl _
  | .head hs h => .head hs h.reaches

theorem Reaches.nsteps (h : Reaches s t) : ∃ n, NSteps n s t := by
  induction h with
  | refl => exact ⟨0, .refl _⟩
  | head hs _ ih => obtain ⟨n, h⟩ := ih; exact ⟨n + 1, .head hs h⟩

/-- On a run of `n` steps to a halted `t`, whatever `s` reaches is at most `n` steps from `t`. -/
theorem NSteps.later (h : NSteps n s t) (ht : step t = none) (hr : Reaches s u) :
    ∃ n' ≤ n, NSteps n' u t := by
  induction hr generalizing n with
  | refl => exact ⟨n, Nat.le_refl _, h⟩
  | head hs _ ih =>
    cases h with
    | refl => rw [ht] at hs; cases hs
    | head hs' h' =>
      rw [hs] at hs'; cases hs'
      obtain ⟨n', hn', h''⟩ := ih h'
      exact ⟨n', by omega, h''⟩

/-- … and whatever it reaches by `Plus`, strictly fewer. -/
theorem NSteps.plus (h : NSteps n s t) (ht : step t = none) (hp : Plus s u) :
    ∃ n' < n, NSteps n' u t := by
  obtain ⟨s₁, hs, hr⟩ := hp
  cases h with
  | refl => rw [ht] at hs; cases hs
  | head hs' h' =>
    rw [hs] at hs'; cases hs'
    obtain ⟨n', hn', h''⟩ := h'.later ht hr
    exact ⟨n', by omega, h''⟩

/-- A run of the runtime that takes `p` transitions moving the machine (`Plus` on the whole
state), and any number that do not. -/
inductive RPath (J : State Value → State Value → Prop) : Nat → RState → RState → Prop
  | refl (x : RState) : RPath J 0 x x
  | move : RStep J x y → Plus x.whole y.whole → RPath J p y z → RPath J (p + 1) x z
  | idle : RStep J x y → ¬ Plus x.whole y.whole → RPath J p y z → RPath J p x z

/-- Every run counts its moves. -/
theorem Star.rpath (h : Star (RStep J) x z) : ∃ p, RPath J p x z := by
  induction h with
  | refl => exact ⟨0, .refl _⟩
  | @head x y _ hs _ ih =>
    obtain ⟨p, h⟩ := ih
    by_cases hp : Plus x.whole y.whole
    · exact ⟨p + 1, .move hs hp h⟩
    · exact ⟨p, .idle hs hp h⟩

theorem RPath.star (h : RPath J p x z) : Star (RStep J) x z := by
  induction h with
  | refl => exact .refl _
  | move hs _ _ ih => exact .head hs ih
  | idle hs _ _ ih => exact .head hs ih

theorem RPath.le (hj : ∀ s t, J s t → Reaches s t) (h : RPath J p x z) (hx : RInv start x)
    (hn : NSteps n x.whole (.dispatch r [])) : p ≤ n := by
  induction h generalizing n with
  | refl => exact Nat.zero_le _
  | move hs hp _ ih =>
    obtain ⟨n', hn', h'⟩ := hn.plus rfl hp
    have := ih (hs.rinv hj hx) h'
    omega
  | idle hs hp _ ih => exact ih (hs.rinv hj hx) ((hs.whole_eq hj hx.2 hp) ▸ hn)

/-- **If the machine returns in `N` steps, no run of the runtime — under any policy, with any
`Reaches`-jets and a sound memo — makes more than `N` transitions that move the machine.** All its
other transitions are bookkeeping or `J s s`; so a policy that idles at most `B` times in a row
takes at most `N + (N + 1) * B` transitions in all (that count is not formalized here). -/
theorem runtime_moves_le (hj : ∀ s t, J s t → Reaches s t) (hm : m.Sound)
    (hN : NSteps N (.reduce a b []) (.dispatch r [])) (h : RPath J p (RState.init a b m) z) :
    p ≤ N :=
  h.le hj (RInv.init hm) hN

/-! ## A concrete policy: anything but `forget` and a redundant `miss` -/

/-- A `miss` at `reduce a b []` right under a memo frame for the same `a` and `b` would push a
second frame for the same application. -/
def RedundantMiss : RState → Prop
  | ⟨.reduce a b [], (a', b', _) :: _, _⟩ => a = a' ∧ b = b'
  | _ => False

/-- Step, `J`-jet, hit, put, and a `miss` that is not redundant, in any order. No `forget`. -/
inductive MemoPolicy (J : State Value → State Value → Prop) : RState → RState → Prop
  | step : step s = some s' → MemoPolicy J ⟨s, fs, m⟩ ⟨s', fs, m⟩
  | jet : J s s' → MemoPolicy J ⟨s, fs, m⟩ ⟨s', fs, m⟩
  | hit : m a b r → MemoPolicy J ⟨.reduce a b k, fs, m⟩ ⟨.dispatch r k, fs, m⟩
  | miss : ¬ RedundantMiss ⟨.reduce a b k, fs, m⟩ →
      MemoPolicy J ⟨.reduce a b k, fs, m⟩ ⟨.reduce a b [], (a, b, k) :: fs, m⟩
  | put : MemoPolicy J ⟨.dispatch r [], (a, b, k) :: fs, m⟩
      ⟨.dispatch r k, fs, fun a' b' r' => m a' b' r' ∨ a' = a ∧ b' = b ∧ r' = r⟩

/-- The idling measure: memo frames left to pop at a `dispatch`; at a `reduce`, whether a `miss`
is still allowed. -/
def idleMeasure : RState → Nat
  | ⟨.dispatch _ _, memos, _⟩ => memos.length
  | ⟨.reduce a b [], (a', b', _) :: _, _⟩ => if a = a' ∧ b = b' then 0 else 1
  | ⟨.reduce _ _ _, _, _⟩ => 1

theorem MemoPolicy.rstep : MemoPolicy J x y → RStep J x y
  | .step h => .step h
  | .jet h => .jet h
  | .hit h => .hit h
  | .miss _ => .miss
  | .put => .put

/-- Under `MemoPolicy`, what does not move the machine is a `miss` or a `put`, and lowers the
measure. -/
theorem MemoPolicy.idle_decreases (hj : ∀ s t, J s t → Plus s t) (hm : x.memo.Sound)
    (h : MemoPolicy J x y) (hp : ¬ Plus x.whole y.whole) : idleMeasure y < idleMeasure x := by
  cases h with
  | step hs => exact absurd (Plus.glue ⟨_, hs, .refl _⟩ _) hp
  | jet hs => exact absurd ((hj _ _ hs).glue _) hp
  | hit hr => exact absurd ((((hm _ _ _ hr).reaches _).plus (by simp)).glue _) hp
  | miss hr =>
    rename_i a b k fs m
    match k, fs with
    | [], (a', b', _) :: _ =>
      have hn : ¬ (a = a' ∧ b = b') := hr
      simp [idleMeasure, hn]
    | [], [] => simp [idleMeasure]
    | _ :: _, _ => simp [idleMeasure]
  | put => simp [idleMeasure]

/-- `MemoPolicy` is a policy, for every `J` that makes progress. -/
theorem memoPolicy (hj : ∀ s t, J s t → Plus s t) (hm : m.Sound) :
    Policy J (MemoPolicy J) a b m where
  sub _ _ h := h.rstep
  live := by
    rintro ⟨s, memos, m'⟩ _ hd
    match s, memos with
    | .reduce _ _ _, _ => exact ⟨_, .step rfl⟩
    | .dispatch _ (.applyTo _ :: _), _ => exact ⟨_, .step rfl⟩
    | .dispatch _ (.computeAndApply _ _ :: _), _ => exact ⟨_, .step rfl⟩
    | .dispatch _ [], (_, _, _) :: _ => exact ⟨_, .put⟩
    | .dispatch _ [], [] => exact absurd ⟨_, rfl, rfl⟩ hd
  idle x hx := acc_of_measure (Star (MemoPolicy J) (RState.init a b m)) idleMeasure
    (fun _ _ hx h => hx.tail h.1)
    (fun _ _ hx h => h.1.idle_decreases hj
      (hx.rinv (fun s t h => (hj s t h).reaches) (fun _ _ h => h.rstep) (RInv.init hm)).2 h.2.1)
    x hx

/-- Instantiated: under `MemoPolicy`, the runtime returns `r` iff the machine does. -/
theorem memoPolicy_returns_iff (hj : ∀ s t, J s t → Plus s t) (hm : m.Sound) :
    (∃ m', Star (MemoPolicy J) (RState.init a b m) ⟨.dispatch r [], [], m'⟩) ↔ Run a b r :=
  runtime_returns_iff (memoPolicy hj hm) (fun s t h => (hj s t h).reaches) hm

/-- Where `Plus` is needed: with the stuttering `Reaches`-jet `J = Eq`, `MemoPolicy` loops on a
program the machine returns from in one step. -/
theorem memoPolicy_eq_jet_loops :
    Run .leaf .leaf (.stem .leaf) ∧
      ¬ Acc (fun y x => MemoPolicy (· = ·) x y ∧ ¬ x.Done) (RState.init .leaf .leaf m) :=
  ⟨.single rfl, fun h => acc_not_self h ⟨.jet rfl, fun ⟨_, e, _⟩ => by cases e⟩⟩

/-! ## Which jets make progress -/

/-- A rule checked at length `ℓ + 1` takes at least one step on every instance. -/
theorem checkRule_plus {ρ : Rule} (h : checkRule ρ (ℓ + 1) = true) (σ : Nat → Value) :
    Plus (ρ.lhs.map (subst σ)) (ρ.rhs.map (subst σ)) := by
  have hs := checkRule_steps h σ
  simp only [steps, Option.bind_eq_some_iff] at hs
  obtain ⟨t, ht, hr⟩ := hs
  exact ⟨t, ht, steps_reaches hr⟩

/-- The only checked rules that may not: length `0`, whose sides are equal — a stutter, which a
rule checker should refuse. -/
theorem checkRule_zero (s : State SValue) : checkRule ⟨s, s⟩ 0 = true := by
  simp [checkRule, ssteps]

/-- Rules that each take a step are `Plus`-jets, under any stack. -/
theorem RuleJet.plus {rules : List Rule}
    (h : ∀ ρ ∈ rules, ∀ σ, Plus (ρ.lhs.map (subst σ)) (ρ.rhs.map (subst σ))) :
    RuleJet rules s t → Plus s t
  | .fire hρ => (h _ hρ _).app _

theorem Value.fork_ne_right : Value.fork c t ≠ t := fun h => by
  have := congrArg sizeOf h; simp at this

/-- The drop-through jet takes a step in each of its three transitions. -/
theorem DropJet.plus (h : checkDropThrough F ks n = true) (hj : DropJet F ks s t) : Plus s t :=
  (DropJet.reaches h hj).plus (by cases hj <;> simp [Value.fork_ne_right])

/-! ## A term-sound jet that is not a `Reaches`-jet: peek.hpp's `S K y b ⟶ b` -/

namespace Peek

/-- `K = △ △`. -/
def K : Value := .stem .leaf
/-- `I = S K K`. -/
def I : Value := .fork (.stem K) K
/-- `ω = S I I`: `ω x = x x`. -/
def ω : Value := .fork (.stem I) I
def Kω : Value := .fork .leaf ω
/-- `Y = S (K ω) (K ω)`: `Y b = ω ω` for every `b`. -/
def Y : Value := .fork (.stem Kω) Kω

/-- peek.hpp's row `△ (△ (△ △)) y @ b = b`: answer `S K y b` with `b`, without running `y b`. -/
inductive PeekJet : State Value → State Value → Prop
  | fire : PeekJet (.reduce (.fork (.stem K) y) b k) (.dispatch b k)

/-- It is term-sound: `S K y b ⟶ K b (y b) ⟶ b`, for all terms `y`, `b`. -/
theorem term_sound (y b : Term) : Steps (△ ⬝ (△ ⬝ (△ ⬝ △)) ⬝ y ⬝ b) b :=
  .head (.root .s) (.head (.root .k) (.refl _))

def stepN : Nat → State Value → Option (State Value)
  | 0, s => some s
  | n + 1, s => (step s).bind (stepN n)

theorem stepN_reaches : ∀ n {s t}, stepN n s = some t → Reaches s t
  | 0, _, _, h => by cases h; exact .refl _
  | n + 1, s, _, h => by
    simp only [stepN] at h
    cases hs : step s with
    | none => rw [hs] at h; cases h
    | some _ => rw [hs] at h; exact .head hs (stepN_reaches n h)

/-- Where the machine is after 5 steps of `S K Y b`: about to run `ω ω`. -/
def cyc (b : Value) : State Value := .dispatch ω [.applyTo ω, .computeAndApply K b]

theorem reach_cyc (b : Value) : Reaches (.reduce (.fork (.stem K) Y) b []) (cyc b) :=
  stepN_reaches 5 rfl

/-- `ω ω` comes back to itself after 15 steps. -/
theorem cyc_cycle (b : Value) : Plus (cyc b) (cyc b) :=
  ⟨_, rfl, stepN_reaches 14 rfl⟩

/-- The machine never returns from `S K Y b`. -/
theorem machine_diverges (b r : Value) : ¬ Run (.fork (.stem K) Y) b r := fun h =>
  (cyc_cycle b).cycle_no_return (h.det rfl (reach_cyc b))

/-- The runtime with the peek jet returns `b` from it, in one transition. -/
theorem runtime_returns (b : Value) (m : Memo) :
    Star (RStep PeekJet) (RState.init (.fork (.stem K) Y) b m) ⟨.dispatch b [], [], m⟩ :=
  .single (.jet .fire)

/-- It is a `Reaches`-transition exactly where the machine's own detour through `y b` returns:
eager-exact on those instances, and only on those. -/
theorem reaches_of_run (h : Run y b v) (k : List (Frame Value)) :
    Reaches (.reduce (.fork (.stem K) y) b k) (.dispatch b k) :=
  .head rfl <| (show Reaches (.reduce y b (.computeAndApply K b :: k))
      (.dispatch v (.computeAndApply K b :: k)) by
    simpa [State.app] using h.app (.computeAndApply K b :: k)).trans <|
    .head rfl <| .head rfl <| .head rfl <| .single rfl

/-- So it is not a `Reaches`-jet: `runtime_sound` does not apply to it. -/
theorem not_reaches : ¬ ∀ s t, PeekJet s t → Reaches s t := fun h =>
  machine_diverges .leaf .leaf (h _ _ .fire)

/-- And it is not `runtime_sound` at all: the runtime answers an application `Apply` has no
answer for. -/
theorem not_sound : ¬ ∀ a b r m m', m.Sound →
    Star (RStep PeekJet) (RState.init a b m) ⟨.dispatch r [], [], m'⟩ →
    Apply a.toTerm b.toTerm r.toTerm := fun h =>
  machine_diverges .leaf .leaf (run_iff_apply.2 (h _ _ _ (fun _ _ _ => False) _
    (fun _ _ _ h => h.elim) (runtime_returns .leaf _)))

end Peek

end TreeCalculus
