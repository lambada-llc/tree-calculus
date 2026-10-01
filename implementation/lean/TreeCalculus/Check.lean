import TreeCalculus.Symbolic

/-!
# Checkers for jets

`explore` runs a tree symbolically, splitting a variable into leaf, stem and fork where a rule
depends on its shape; `var 1` is the element a jet inspects, `var 0` a tail no branch may inspect.
By `explore_sound`, `checkRun` and `checkDropThrough`, decided by the kernel, hold for every
argument; `DropJet.reaches` makes a checked `DropJet` a jet of `runtime_sound`.
-/

namespace TreeCalculus

open Term
open SValue

/-- A branch's verdict on the state it reached, given what `var 1` has been refined to:
`some ok` ends the branch, `none` runs it on. -/
abbrev Verdict := SValue → State SValue → Option Bool

/-- A case split: the three shapes a value can have, over the variables `j` and `j + 1`. -/
def shapes (j : Nat) : List SValue := [lit .leaf, stem (var j), fork (var j) (var (j + 1))]

/-- Run `s` — reached with `var 1` refined to `pat` — until the verdict ends it, for at most `n`
steps along any one branch. A rule that depends on a variable other than `var 0` splits the
branch into `shapes`, fresh variables numbered past every one in `pat`. `true` iff every branch
ends accepted. -/
def explore (verdict : Verdict) : Nat → SValue → State SValue → Bool
  | 0, _, _ => false
  | n + 1, pat, s => match verdict pat s with
    | some ok => ok
    | none => match sstep s with
      | .next s' => explore verdict n pat s'
      | .halt => false
      | .split i => i != 0 && (shapes (pat.maxVar + 1)).all fun p =>
          explore verdict n (pat.inst i p) (s.map (·.inst i p))

/-- What a branch knows: every instance — `var 1` at an instance of `pat`, `var 0` anything —
runs from `init c t` to the matching instance of `s`. -/
def Inv (init : Value → Value → State Value) (pat : SValue) (s : State SValue) : Prop :=
  ∀ σ, Reaches (init (pat.subst σ) (σ 0)) (s.map (subst σ))

/-- Some shape of `shapes` covers whatever `σ` sends `i` to, without disturbing `pat`'s instance. -/
theorem shapes_cover (pat : SValue) (σ : Nat → Value) (i : Nat) :
    ∃ p ∈ shapes (pat.maxVar + 1), ∃ σ', (pat.inst i p).subst σ' = pat.subst σ ∧ σ' 0 = σ 0 := by
  have agree : ∀ σ', σ' 0 = σ 0 → (∀ j ≤ pat.maxVar, σ' j = σ j) → ∀ p : SValue, p.subst σ' = σ i →
      (pat.inst i p).subst σ' = pat.subst σ := by
    intro σ' _ h p hp; rw [subst_inst, hp]; apply subst_congr; intro j hj
    by_cases hji : j = i <;> simp [upd, hji, h j hj]
  generalize hj : pat.maxVar + 1 = j
  match hσi : σ i with
  | .leaf => exact ⟨_, .head _, σ, agree σ rfl (fun _ _ => rfl) (lit .leaf) hσi.symm, rfl⟩
  | .stem v =>
    refine ⟨_, .tail _ (.head _), upd σ j v, ?_, by simp [upd]; omega⟩
    exact agree _ (by simp [upd]; omega) (fun x hx => by simp [upd, show x ≠ j by omega])
      (stem (var j)) (by simp [subst, upd, hσi])
  | .fork v w =>
    refine ⟨_, .tail _ (.tail _ (.head _)), upd (upd σ j v) (j + 1) w, ?_, by simp [upd]; omega⟩
    exact agree _ (by simp [upd]; omega)
      (fun x hx => by simp [upd, show x ≠ j by omega, show x ≠ j + 1 by omega])
      (fork (var j) (var (j + 1))) (by simp [subst, upd, hσi])

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
def pinnedIn (pat : SValue) (ks : List Value) : Bool := ks.any (pat.fits false ·)

/-- `c`, refined to `pat`, is none of `ks`. -/
def avoids (pat : SValue) (ks : List Value) : Bool := ks.all (!pat.fits true ·)

theorem pinnedIn_mem (h : pinnedIn pat ks = true) (σ) : pat.subst σ ∈ ks := by
  obtain ⟨k, hk, hfit⟩ := List.any_eq_true.mp h; rwa [fits_false hfit]

theorem avoids_not_mem (h : avoids pat ks = true) (σ) : pat.subst σ ∉ ks := fun hk => by
  have := List.all_eq_true.mp h _ hk; rw [fits_true rfl] at this; cases this

/-! ## Running a closed application -/

/-- `F x` runs to `r`. -/
def checkRun (F x r : Value) (n : Nat) : Bool :=
  explore (fun _ s => match s with | .dispatch v [] => some (v.fits false r) | _ => none)
    n (var 1) (.reduce (lit F) (lit x) [])

theorem checkRun_sound (h : checkRun F x r n = true) : Apply F.toTerm x.toTerm r.toTerm := by
  refine run_iff_apply.1 ?_
  obtain ⟨_, s, σ, hv, hinv, -⟩ :=
    explore_sound (init := fun _ _ => .reduce F x []) h (fun _ => .refl _) (fun _ => .leaf)
  split at hv
  · simpa [Run, State.map, fits_false (Option.some.inj hv)] using hinv σ
  · cases hv

/-! ## Drop-through jets: a loop that discards a list up to a separator -/

/-- `F` is a loop over a list that drops elements up to and including the first in `ks`: `F △`
is `△`; on `△ c t` every branch either tail-calls `F t` (`c` provably none of `ks`) or returns
`t` itself (`c` pinned to one of `ks`); `t` is never looked at. -/
def checkDropThrough (F : Value) (ks : List Value) (n : Nat) : Bool :=
  checkRun F .leaf .leaf n &&
  explore (fun pat s => match s with
      | .reduce a (.var 0) [] => if a.fits false F then some (avoids pat ks) else none
      | .dispatch (.var 0) [] => some (pinnedIn pat ks)
      | _ => none)
    n (var 1) (.reduce (lit F) (fork (var 1) (var 0)) [])

theorem checkDropThrough_fork (h : checkDropThrough F ks n = true) (c t : Value) :
    c ∉ ks ∧ Reaches (.reduce F (.fork c t) []) (.reduce F t []) ∨
    c ∈ ks ∧ Apply F.toTerm (Value.fork c t).toTerm t.toTerm := by
  simp only [checkDropThrough, Bool.and_eq_true] at h
  obtain ⟨pat, s, σ, hv, hinv, hc, ht⟩ :=
    explore_sound (init := fun c t => .reduce F (.fork c t) []) h.2 (fun _ => .refl _)
      (fun i => if i = 0 then t else c)
  simp only [subst, Nat.one_ne_zero, ↓reduceIte] at hc ht
  have run := hinv σ; simp only [State.map, hc, ht] at run
  split at hv
  · split at hv
    · simp only [Option.some.injEq] at hv
      exact .inl ⟨hc ▸ avoids_not_mem hv σ, by simpa [fits_false ‹_›, subst, ht] using run⟩
    · cases hv
  · simp only [Option.some.injEq] at hv
    exact .inr ⟨hc ▸ pinnedIn_mem hv σ, run_iff_apply.1 (by simpa [Run, subst, ht] using run)⟩
  · cases hv

/-- What a runtime may do with a drop-through jet: skip an element that is none of `ks` and go on
with the rest — natively, or by handing it back to the tree — or answer at one that is, or at the
end of the list. -/
inductive DropJet (F : Value) (ks : List Value) : State Value → State Value → Prop
  | skip : c ∉ ks → DropJet F ks (.reduce F (.fork c t) k) (.reduce F t k)
  | hit : c ∈ ks → DropJet F ks (.reduce F (.fork c t) k) (.dispatch t k)
  | nil : DropJet F ks (.reduce F .leaf k) (.dispatch .leaf k)

theorem DropJet.reaches (h : checkDropThrough F ks n = true) : DropJet F ks s t → Reaches s t
  | .skip hc => by
    rcases checkDropThrough_fork h _ _ with ⟨-, hs⟩ | ⟨hk, -⟩
    · simpa [State.app] using hs.app _
    · exact absurd hk hc
  | .hit hk => by
    rcases checkDropThrough_fork h _ _ with ⟨hk', -⟩ | ⟨-, hs⟩
    · exact absurd hk hk'
    · exact hs.reaches _
  | .nil => (checkRun_sound (Bool.and_eq_true _ _ ▸ h).1).reaches _

end TreeCalculus
