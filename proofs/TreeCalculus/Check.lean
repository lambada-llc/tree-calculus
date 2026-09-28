import TreeCalculus.Symbolic

/-!
# Verified checkers for jets

A jet is a native fast path the runtime takes when it sees a known tree applied to an argument.
Each checker below symbolically runs that tree on an argument with variables, case-splitting a
variable only where a rule depends on its shape, and returns `true` only if every branch lands
where the jet says it does. Their soundness theorems turn `check … = true` — a closed `Bool`
the kernel evaluates (`decide +kernel`) — into a statement about `Run`, i.e. about every
concrete argument.

The argument is always `var 1` (the part the jet inspects, called `c`) and, for jets that
recurse into a tail, `var 0` (`t`, which must never be inspected). `explore` does the running and
splitting; each checker only says which states end a branch, and how (a `Verdict`).
-/

namespace TreeCalculus
open STree

/-- A branch's verdict on the state it reached, given what `var 1` has been refined to:
`some ok` ends the branch, `none` runs it on. -/
abbrev Verdict := STree → State STree → Option Bool

/-- A case split: the three shapes a tree can have, over the variables `j` and `j + 1`. -/
def shapes (j : Nat) : List STree := [lit .leaf, stem (var j), fork (var j) (var (j + 1))]

/-- Run `s` — reached with `var 1` refined to `pat` — until the verdict ends it, for at most `n`
steps along any one branch. A rule that depends on a variable other than `var 0` splits the
branch into `shapes`, fresh variables numbered past every one in `pat`. `true` iff every branch
ends accepted. -/
def explore (verdict : Verdict) : Nat → STree → State STree → Bool
  | 0, _, _ => false
  | n + 1, pat, s => match verdict pat s with
    | some ok => ok
    | none => match sstep s with
      | .next s' => explore verdict n pat s'
      | .halt => false
      | .stuck i => i != 0 && (shapes (pat.maxVar + 1)).all fun p =>
          explore verdict n (pat.inst i p) (s.map (·.inst i p))

/-- What a branch knows: every instance — `var 1` at an instance of `pat`, `var 0` anything —
runs from `init c t` to the matching instance of `s`. -/
def Inv (init : Tree → Tree → State Tree) (pat : STree) (s : State STree) : Prop :=
  ∀ σ, Reaches (init (pat.subst σ) (σ 0)) (s.map (subst σ))

/-- Some shape of `shapes` covers whatever `σ` sends `i` to, without disturbing `pat`'s instance. -/
theorem shapes_cover (pat : STree) (σ : Nat → Tree) (i : Nat) :
    ∃ p ∈ shapes (pat.maxVar + 1), ∃ σ', (pat.inst i p).subst σ' = pat.subst σ ∧ σ' 0 = σ 0 := by
  have agree : ∀ σ', σ' 0 = σ 0 → (∀ j ≤ pat.maxVar, σ' j = σ j) → ∀ p : STree, p.subst σ' = σ i →
      (pat.inst i p).subst σ' = pat.subst σ := by
    intro σ' _ h p hp; rw [subst_inst, hp]; apply subst_congr; intro j hj
    by_cases hji : j = i <;> simp [upd, hji, h j hj]
  generalize hj : pat.maxVar + 1 = j
  match hσ : σ i with
  | .leaf => exact ⟨_, .head _, σ, agree σ rfl (fun _ _ => rfl) (lit .leaf) hσ.symm, rfl⟩
  | .stem v =>
    refine ⟨_, .tail _ (.head _), upd σ j v, ?_, by simp [upd]; omega⟩
    exact agree _ (by simp [upd]; omega) (fun x hx => by simp [upd, show x ≠ j by omega])
      (stem (var j))
      (by simp [subst, upd, hσ])
  | .fork v w =>
    refine ⟨_, .tail _ (.tail _ (.head _)), upd (upd σ j v) (j + 1) w, ?_, by simp [upd]; omega⟩
    exact agree _ (by simp [upd]; omega)
      (fun x hx => by simp [upd, show x ≠ j by omega, show x ≠ j + 1 by omega])
      (fork (var j) (var (j + 1))) (by simp [subst, upd, hσ])

/-- `explore` accepting means every instance of the start is covered by a branch the verdict
accepted, with that branch's `Inv`. -/
theorem explore_sound (h : explore verdict n pat s = true) (hinv : Inv init pat s) (σ) :
    ∃ pat' s' σ', verdict pat' s' = some true ∧ Inv init pat' s' ∧
      pat'.subst σ' = pat.subst σ ∧ σ' 0 = σ 0 := by
  induction n generalizing pat s σ with
  | zero => simp [explore] at h
  | succ n ih =>
    simp only [explore] at h
    split at h
    · exact ⟨pat, s, σ, by simp_all, hinv, rfl, rfl⟩
    · split at h
      · exact ih h (fun σ => (hinv σ).trans (.single (sstep_next ‹_› σ))) σ
      · cases h
      · rename_i i _
        simp only [Bool.and_eq_true, bne_iff_ne, ne_eq, List.all_eq_true] at h
        obtain ⟨hi, hall⟩ := h
        obtain ⟨p, hp, σ', hpat, h0⟩ := shapes_cover pat σ i
        have hinv' : Inv init (pat.inst i p) (s.map (·.inst i p)) := fun σ => by
          have := hinv (upd σ i (p.subst σ))
          rwa [← subst_inst, ← State.subst_inst, show upd σ i (p.subst σ) 0 = σ 0 by
            simp [upd]; omega] at this
        obtain ⟨pat', s', σ'', hv, hinv'', hpat', h0'⟩ := ih (hall p hp) hinv' σ'
        exact ⟨pat', s', σ'', hv, hinv'', hpat'.trans hpat, h0'.trans h0⟩

/-! ## What a branch's pattern says about `c` -/

/-- `c`, refined to `pat`, is one of `ks` whatever the rest of it is. -/
def pinnedIn (pat : STree) (ks : List Tree) : Bool := ks.any (pat.fits false ·)

/-- `c`, refined to `pat`, is none of `ks`. -/
def avoids (pat : STree) (ks : List Tree) : Bool := ks.all (!pat.fits true ·)

theorem pinnedIn_mem (h : pinnedIn pat ks = true) (σ) : pat.subst σ ∈ ks := by
  obtain ⟨k, hk, hfit⟩ := List.any_eq_true.mp h; rwa [fits_false hfit]

theorem avoids_not_mem (h : avoids pat ks = true) (σ) : pat.subst σ ∉ ks := fun hk => by
  have := List.all_eq_true.mp h _ hk; rw [fits_true rfl] at this; cases this

/-! ## Running a closed application -/

/-- `F x` runs to `r`. -/
def checkRun (F x r : Tree) (n : Nat) : Bool :=
  explore (fun _ s => match s with | .dispatch v [] => some (v.fits false r) | _ => none)
    n (var 1) (.reduce (lit F) (lit x) [])

theorem checkRun_sound (h : checkRun F x r n = true) : Run F x r := by
  obtain ⟨_, s, σ, hv, hinv, -⟩ :=
    explore_sound (init := fun _ _ => .reduce F x []) h (fun _ => .refl) (fun _ => .leaf)
  split at hv
  · simpa [Run, State.map, fits_false (Option.some.inj hv)] using hinv σ
  · cases hv

/-! ## Predicate jets: "a predicate on chars plus a char" -/

/-- `P` decides membership in `ks`: every branch returns, true where `c` has been pinned to one
of `ks` and false where it provably is none of them. -/
def checkMember (P : Tree) (ks : List Tree) (n : Nat) : Bool :=
  explore (fun pat s => match s with
      | .dispatch v [] => some <|
          if pinnedIn pat ks then v.fits false .true else avoids pat ks && v.fits false .false
      | _ => none)
    n (var 1) (.reduce (lit P) (var 1) [])

theorem checkMember_sound (h : checkMember P ks n = true) (c : Tree) :
    Run P c (if c ∈ ks then .true else .false) := by
  obtain ⟨pat, s, σ, hv, hinv, hc, -⟩ :=
    explore_sound (init := fun c _ => .reduce P c []) h (fun _ => .refl) (fun _ => c)
  simp only [subst] at hc
  split at hv
  · have run := hinv σ; simp only [State.map, List.map_nil, hc] at run
    simp only [Option.some.injEq] at hv
    split at hv
    · rw [fits_false hv] at run; simpa [Run, hc ▸ pinnedIn_mem ‹_› σ] using run
    · simp only [Bool.and_eq_true] at hv
      rw [fits_false hv.2] at run; simpa [Run, hc ▸ avoids_not_mem hv.1 σ] using run
  · cases hv

/-- `equal_const k`'s specification. -/
theorem checkMember_single (h : checkMember P [k] n = true) (c : Tree) :
    Run P c (if c = k then .true else .false) := by
  simpa using checkMember_sound h c

/-- What the runtime may do with a membership jet: answer `apply(P, c)` outright. -/
inductive MemberJet (P : Tree) (ks : List Tree) : State Tree → State Tree → Prop
  | answer : MemberJet P ks (.reduce P c k) (.dispatch (if c ∈ ks then .true else .false) k)

theorem MemberJet.reaches (h : checkMember P ks n = true) : MemberJet P ks s t → Reaches s t
  | .answer => (checkMember_sound h _).frame _

/-! ## Drop-through jets: a loop that discards a list up to a separator -/

inductive IsList : Tree → Prop
  | nil : IsList .leaf
  | cons (h : Tree) : IsList t → IsList (.fork h t)

/-- `xs` without its elements up to and including the first one in `ks`. -/
def dropThrough (ks : List Tree) : Tree → Tree
  | .fork h t => if h ∈ ks then t else dropThrough ks t
  | t => t

/-- `F` is a loop over a list that drops elements up to and including the first in `ks`: `F △`
is `△`; on `△ c t` every branch either tail-calls `F t` (`c` provably none of `ks`) or returns
`t` itself (`c` pinned to one of `ks`); `t` is never looked at. -/
def checkDropThrough (F : Tree) (ks : List Tree) (n : Nat) : Bool :=
  checkRun F .leaf .leaf n &&
  explore (fun pat s => match s with
      | .reduce a (.var 0) [] => if a.fits false F then some (avoids pat ks) else none
      | .dispatch (.var 0) [] => some (pinnedIn pat ks)
      | _ => none)
    n (var 1) (.reduce (lit F) (fork (var 1) (var 0)) [])

theorem checkDropThrough_fork (h : checkDropThrough F ks n = true) (c t : Tree) :
    c ∉ ks ∧ Reaches (.reduce F (.fork c t) []) (.reduce F t []) ∨
    c ∈ ks ∧ Run F (.fork c t) t := by
  simp only [checkDropThrough, Bool.and_eq_true] at h
  obtain ⟨pat, s, σ, hv, hinv, hc, ht⟩ :=
    explore_sound (init := fun c t => .reduce F (.fork c t) []) h.2 (fun _ => .refl)
      (fun i => if i = 0 then t else c)
  simp only [subst, Nat.one_ne_zero, ↓reduceIte] at hc ht
  have run := hinv σ; simp only [State.map, hc, ht] at run
  split at hv
  · split at hv
    · simp only [Option.some.injEq] at hv
      exact .inl ⟨hc ▸ avoids_not_mem hv σ, by simpa [fits_false ‹_›, subst, ht] using run⟩
    · cases hv
  · simp only [Option.some.injEq] at hv
    exact .inr ⟨hc ▸ pinnedIn_mem hv σ, by simpa [Run, subst, ht] using run⟩
  · cases hv

/-- A native loop may skip any element not in `ks` and hand the rest back to the tree. -/
theorem checkDropThrough_skip (h : checkDropThrough F ks n = true) (hc : c ∉ ks)
    (ht : Run F t r) : Run F (.fork c t) r := by
  rcases checkDropThrough_fork h c t with ⟨-, hs⟩ | ⟨hk, -⟩
  · exact hs.trans ht
  · exact absurd hk hc

theorem checkDropThrough_hit (h : checkDropThrough F ks n = true) (hk : k ∈ ks) (t : Tree) :
    Run F (.fork k t) t := by
  rcases checkDropThrough_fork h k t with ⟨hk', -⟩ | ⟨-, hs⟩
  · exact absurd hk hk'
  · exact hs

/-- What the runtime may do with a drop-through jet: skip an element that is none of `ks` and go
on with the rest — natively, or by handing it back to the tree — or answer at one that is, or at
the end of the list. -/
inductive DropJet (F : Tree) (ks : List Tree) : State Tree → State Tree → Prop
  | skip : c ∉ ks → DropJet F ks (.reduce F (.fork c t) k) (.reduce F t k)
  | hit : c ∈ ks → DropJet F ks (.reduce F (.fork c t) k) (.dispatch t k)
  | nil : DropJet F ks (.reduce F .leaf k) (.dispatch .leaf k)

theorem DropJet.reaches (h : checkDropThrough F ks n = true) : DropJet F ks s t → Reaches s t
  | .skip hc => by
    rcases checkDropThrough_fork h _ _ with ⟨-, hs⟩ | ⟨hk, -⟩
    · simpa [State.app] using hs.app _
    · exact absurd hk hc
  | .hit hk => (checkDropThrough_hit h hk _).frame _
  | .nil => (checkRun_sound (Bool.and_eq_true _ _ ▸ h).1).frame _

theorem checkDropThrough_list (h : checkDropThrough F ks n = true) (hxs : IsList xs) :
    Run F xs (dropThrough ks xs) := by
  induction hxs with
  | nil => exact checkRun_sound (Bool.and_eq_true _ _ ▸ h).1
  | cons c _ ih =>
    by_cases hc : c ∈ ks
    · simpa [dropThrough, hc] using checkDropThrough_hit h hc _
    · simpa [dropThrough, hc] using checkDropThrough_skip h hc ih

end TreeCalculus
